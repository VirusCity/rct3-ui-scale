#include "superwide_fix.h"

#include <windows.h>

#include <d3d9.h>

#include <cstring>

#include "../core/canvas_probe.h"  // SafeRead only — no discovery dependency
#include "../core/config.h"
#include "../core/log.h"
#include "../core/patch.h"
#include "../core/state.h"  // backbuffer dims only — no discovery dependency
#include "../sig/sigscan.h"

namespace superwide {
namespace {

// The UI quad allocator (Complete FUN_0079c200, Gold FUN_00695e80) is a
// __stdcall(3) that returns "vertexBufferBase + slot*8", or 0 when the clipper
// rejects the quad. We locate it by that pointer-return epilogue —
//   MOV EAX,[slot+0x30]; MOV base,[abs]; ...; LEA EAX,[base + slot*8]; RET 0xC
// — the semantic that separates it from same-pool SIBLING allocators which
// return void (hooking those with a pointer detour would imbalance the stack).
// Wildcards cover only the edition-specific vertex-buffer-base operand; the
// prologue walks the match back to the function entry (same idiom as
// timing_hook). Complete is SSE-compiled, Gold x87 — hence two built-ins.
struct AllocSig {
  const char* body;      // FindUnique anchor: the pointer-return epilogue
  const char* prologue;  // walk back from the anchor to the entry (null = entry)
  const char* edition;
};

constexpr AllocSig kBuiltins[] = {
    {"A1 ?? ?? ?? ?? 8D 04 C8 5F 5E 5B 5D C2 0C 00", "55 8B EC 53 66 8B 1D",
     "Complete-style"},
    {"8B 46 30 8B 0D ?? ?? ?? ?? 5F 5E 8D 04 C1 5B C2 0C 00",
     "53 56 57 66 8B 3D", "Gold-style"},
};
constexpr size_t kWalkBack = 0x180;

// __stdcall(3), returns the slot pointer (or, unpatched, NULL). Matches both
// editions (verified: RET 0xC, 3 args).
using AllocFn = void*(__stdcall*)(void*, int, void*);
AllocFn g_orig = nullptr;
void* g_scratch = nullptr;

void* __stdcall Detour(void* a, int b, void* c) {
  void* r = g_orig(a, b, c);
  // NULL == the engine rejected this quad (off-screen/degenerate); it was NOT
  // added to the draw batch. Hand back scratch so the unchecked caller writes
  // there harmlessly instead of to address 0. Nothing renders; nothing crashes.
  return r ? r : g_scratch;
}

// Guarded 4-byte write to a heap rect field (the canvas object is RW heap; the
// VirtualProtect is belt-and-suspenders, mirroring patch::ApplyFloat).
bool WriteFloat(void* addr, float v) {
  DWORD oldProt;
  if (!VirtualProtect(addr, 4, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
  memcpy(addr, &v, 4);
  DWORD ignored;
  VirtualProtect(addr, 4, oldProt, &ignored);
  return true;
}

// ---------------------------------------------------------------------------
// Cosmetic layer: pillarbox the loading screen on too-wide displays.
//
// The loading screen is laid out against a FIXED 1280x1024 design space, fitted
// with scale = min(1280/canvasW, 1024/canvasH). Because 1280/1024 = 1.25 is
// narrower than any real display, the min always picks the WIDTH term, so
// vertical positions scale with canvas WIDTH — past ~2.78:1 the progress bar is
// pushed off the bottom (that's the crash the allocator guard now absorbs, but
// the bar is then simply invisible: a blank screen with no loading feedback).
//
// So we clamp the canvas the loader lays out against to at most
// SuperUltrawideLoaderAspect (default 16:9) for the duration of the call, then
// restore it — gameplay/UI-scale never see it. 16:9 rather than the authored 5:4
// because the clamp engages on anything WIDER than the limit, and 5:4 would
// pillarbox ordinary 16:9 monitors that never had the bug. 16:9 is also exactly
// what the engine already does at a stock 1080p (width-bound, so only design-Y
// <= 720 is ever shown) — i.e. the presentation every 16:9 player has always
// seen — so nothing is lost by holding wider displays to it.
//
// Located per edition by the loader's own canvas-rect divide, which begins with
// MOV EAX,[canvasGlobal] — so the canvas pointer is read straight OUT of the
// matched code. That keeps this layer independent of the mod's canvas discovery
// (it works on a first, cache-less launch). Complete is SSE, Gold x87.
struct LoaderSig {
  const char* body;      // begins with A1 <canvasGlobal> (operand at +1)
  const char* prologue;  // walk back from the anchor to the function entry
  const char* edition;
};

constexpr LoaderSig kLoaderSigs[] = {
    // MOV EAX,[canvas]; MOVSS xmm0,[1280]; xmm1=[+0x84]-[+0x7c]; DIVSS;
    // xmm1=[+0x88]-[+0x80]
    {"A1 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? F3 0F 10 88 84 00 00 00 "
     "F3 0F 5C 48 7C F3 0F 5E C1 F3 0F 10 88 88 00 00 00 F3 0F 5C 88 80 00 00 "
     "00",
     "55 8B EC 6A FF 68", "Complete-style"},
    // MOV EAX,[canvas]; FLD [+0x84]; FSUB [+0x7c]; FDIVR [1280]; FSTP;
    // FLD [+0x88]; FSUB [+0x80]  (register pushes are interleaved by the x87
    // scheduler — they are fixed bytes, so they stay in the pattern)
    {"A1 ?? ?? ?? ?? D9 80 84 00 00 00 55 D8 60 7C 56 57 8B F9 D8 3D ?? ?? ?? "
     "?? D9 5C 24 28 D9 80 88 00 00 00 D8 A0 80 00 00 00",
     "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25", "Gold-style"},
};

// Canvas rect field offsets. Not assumed — they appear as literal displacement
// bytes in both signatures above, so a match guarantees these are correct.
constexpr uint32_t kRectLeft = 0x7c, kRectTop = 0x80;
constexpr uint32_t kRectRight = 0x84, kRectBottom = 0x88;
constexpr size_t kLoaderWalkBack = 0x60;

using LoaderFn = void(__fastcall*)(void*, void*);
LoaderFn g_loaderOrig = nullptr;
uintptr_t g_canvasGlobal = 0;  // VA of the pointer TO the canvas object
bool g_loggedPillar = false;
thread_local int t_loaderDepth = 0;

// Set when a clamp was applied this frame; consumed by OnPresent. g_boxFrac is
// the fraction of the canvas width the pillarboxed box occupies — the engine maps
// canvas x -> device x by (deviceW / canvasW) with no origin term, so the box
// ends at exactly g_boxFrac * backbufferWidth device pixels.
bool g_pillarActive = false;
float g_boxFrac = 0.f;
bool g_loggedFill = false;

bool ReadU32(uintptr_t va, uint32_t* out) {
  return probe::SafeRead(va, out, sizeof(*out));
}
bool ReadF32(uintptr_t va, float* out) {
  return probe::SafeRead(va, out, sizeof(*out));
}

// Narrow the loader's layout canvas to the configured max aspect for this call.
// Returns whether a clamp was applied; on true reports the edge VA + the
// original value to restore.
bool ClampLoaderCanvas(uintptr_t* rightVA, float* savedRight) {
  if (!g_canvasGlobal) return false;
  uint32_t obj = 0;
  if (!ReadU32(g_canvasGlobal, &obj) || !obj) return false;  // canvas not built
  float l, t, r, b;
  if (!ReadF32(obj + kRectLeft, &l) || !ReadF32(obj + kRectTop, &t) ||
      !ReadF32(obj + kRectRight, &r) || !ReadF32(obj + kRectBottom, &b))
    return false;
  const float w = r - l, h = b - t;
  if (h <= 1.f || w <= 1.f) return false;
  const float maxAspect = GetConfig().superUltrawideLoaderAspect;
  if (w / h <= maxAspect) return false;  // display is not too wide — leave it

  const float newRight = l + maxAspect * h;
  const uintptr_t va = obj + kRectRight;
  if (!WriteFloat(reinterpret_cast<void*>(va), newRight)) return false;
  *rightVA = va;
  *savedRight = r;
  // Tell OnPresent to fill the margin beside the box this frame.
  g_boxFrac = (maxAspect * h) / w;
  g_pillarActive = true;
  if (!g_loggedPillar) {
    g_loggedPillar = true;
    LOG("superwide: loading screen pillarboxed — canvas %.0fx%.0f (aspect "
        "%.3f) laid out as %.0fx%.0f (%.4f:1); restored after the frame",
        w, h, w / h, newRight - l, h, maxAspect);
  }
  return true;
}

void __fastcall LoaderDetour(void* ecx, void* edx) {
  if (t_loaderDepth != 0) {  // nested — outermost call already clamped
    g_loaderOrig(ecx, edx);
    return;
  }
  ++t_loaderDepth;
  uintptr_t rightVA = 0;
  float savedRight = 0.f;
  const bool clamped = ClampLoaderCanvas(&rightVA, &savedRight);
  // __finally so the canvas is restored even if the loader unwinds via its own
  // SEH frame — a clamp can never leak into gameplay.
  __try {
    g_loaderOrig(ecx, edx);
  } __finally {
    if (clamped) WriteFloat(reinterpret_cast<void*>(rightVA), savedRight);
    --t_loaderDepth;
  }
}

// FindUnique the body anchor, walk back to the entry, hook. Returns success.
bool TryInstall(const char* body, const char* prologue, const char* what) {
  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) return false;
  sigscan::Pattern bodyPat, proPat;
  if (!sigscan::Compile(body, bodyPat) ||
      (prologue && !sigscan::Compile(prologue, proPat)))
    return false;

  int n = 0;
  uint8_t* anchor = sigscan::FindUnique(exe, bodyPat, &n);
  if (!anchor) {
    if (n > 1)
      LOG("superwide: %s allocator anchor matched %d sites (need 1) — skipped",
          what, n);
    return false;
  }
  uint8_t* entry =
      prologue ? sigscan::FindBackward(anchor, kWalkBack, proPat) : anchor;
  if (!entry) {
    LOG("superwide: %s anchor @ %p but no prologue within 0x%zX — skipped",
        what, static_cast<void*>(anchor), kWalkBack);
    return false;
  }
  if (!patch::Hook(reinterpret_cast<void*>(entry),
                   reinterpret_cast<void*>(&Detour),
                   reinterpret_cast<void**>(&g_orig), "superwide allocator")) {
    LOG("superwide: %s hook install failed", what);
    return false;
  }
  LOG("superwide: armed on %s — UI quad allocator @ exe+0x%X null-guarded "
      "(fixes the >2.78 / 32:9 launch crash; no discovery or cache needed; "
      "gameplay unaffected)",
      what,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(entry) -
                            reinterpret_cast<uintptr_t>(exe)));
  return true;
}

// Locate + hook the loading screen, capturing the canvas-global pointer from the
// matched code. Purely cosmetic: on failure the allocator guard still prevents
// the crash, the bar is just off-screen (pre-pillarbox behaviour).
bool TryInstallLoader(const LoaderSig& s) {
  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) return false;
  sigscan::Pattern bodyPat, proPat;
  if (!sigscan::Compile(s.body, bodyPat) ||
      !sigscan::Compile(s.prologue, proPat))
    return false;

  int n = 0;
  uint8_t* anchor = sigscan::FindUnique(exe, bodyPat, &n);
  if (!anchor) {
    if (n > 1)
      LOG("superwide: %s loader anchor matched %d sites (need 1) — pillarbox "
          "skipped",
          s.edition, n);
    return false;
  }
  uint8_t* entry = sigscan::FindBackward(anchor, kLoaderWalkBack, proPat);
  if (!entry) {
    LOG("superwide: %s loader anchor @ %p but no prologue within 0x%zX — "
        "pillarbox skipped",
        s.edition, static_cast<void*>(anchor), kLoaderWalkBack);
    return false;
  }
  // The matched sequence opens with MOV EAX,[canvasGlobal]; the operand is the
  // VA of the pointer to the canvas object. Read it straight from the code.
  const uint32_t canvasGlobal = sigscan::ReadDwordAt(anchor, 1);
  if (canvasGlobal < reinterpret_cast<uintptr_t>(exe)) {
    LOG("superwide: %s loader canvas operand 0x%08X implausible — pillarbox "
        "skipped",
        s.edition, canvasGlobal);
    return false;
  }
  g_canvasGlobal = canvasGlobal;

  if (!patch::Hook(reinterpret_cast<void*>(entry),
                   reinterpret_cast<void*>(&LoaderDetour),
                   reinterpret_cast<void**>(&g_loaderOrig),
                   "superwide loader")) {
    LOG("superwide: %s loader hook install failed — pillarbox off", s.edition);
    g_canvasGlobal = 0;
    return false;
  }
  LOG("superwide: pillarbox armed on %s — loading screen @ exe+0x%X will lay "
      "out at most %.4f:1 (canvas global 0x%08X, read from code — no cache "
      "needed)",
      s.edition,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(entry) -
                            reinterpret_cast<uintptr_t>(exe)),
      GetConfig().superUltrawideLoaderAspect, canvasGlobal);
  return true;
}

}  // namespace

void Install() {
  const Config& cfg = GetConfig();
  if (!cfg.enabled || !cfg.superUltrawideFix) {
    LOG("superwide: [Borderless] SuperUltrawideFix=0 (or mod disabled) — not "
        "installing the allocator null-guard");
    return;
  }

  // One throwaway page rejected quads write into. A rejected primitive writes at
  // most a few dozen vertices; 64 KB is far beyond that, and its own region so a
  // stray write can never touch mod or game state.
  g_scratch = VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE,
                           PAGE_READWRITE);
  if (!g_scratch) {
    LOG("superwide: scratch alloc failed — feature off (cannot guard safely)");
    return;
  }

  // (1) The crash floor: null-guard the UI quad allocator. INI override (an
  // entry-direct signature) first, then the built-ins. The exe is one edition,
  // so exactly one should match; stop at the first success.
  bool guarded = false;
  if (cfg.sigSuperWide[0])
    guarded = TryInstall(cfg.sigSuperWide, nullptr, "INI override");
  if (!guarded) {
    for (const AllocSig& s : kBuiltins) {
      if (TryInstall(s.body, s.prologue, s.edition)) {
        guarded = true;
        break;
      }
    }
  }
  if (!guarded) {
    LOG("superwide: no known UI-allocator signature matched this build — "
        "feature off (nothing altered, but no >2.78 crash protection here; add "
        "[Signatures] SuperUltrawide to override)");
    VirtualFree(g_scratch, 0, MEM_RELEASE);
    g_scratch = nullptr;
    return;
  }

  // (2) Cosmetic layer: pillarbox the loading screen on too-wide displays so the
  // progress bar stays visible. Strictly optional and only attempted once the
  // guard is in place, so it can never be load-bearing — if no signature
  // matches, the guard alone still prevents the crash and the bar is simply
  // off-screen (exactly the pre-pillarbox behaviour).
  for (const LoaderSig& s : kLoaderSigs)
    if (TryInstallLoader(s)) return;
  LOG("superwide: no loading-screen signature matched this build — pillarbox "
      "off (harmless: the crash guard is armed; the loading bar is just "
      "off-screen above ~2.78:1)");
}

void OnPresent(IDirect3DDevice9* dev) {
  if (!g_pillarActive) return;
  g_pillarActive = false;  // one fill per pillarboxed frame
  if (!dev) return;

  unsigned bbW = 0, bbH = 0;
  if (!state::GetBackbuffer(&bbW, &bbH) || !bbW || !bbH) return;
  if (g_boxFrac <= 0.f || g_boxFrac >= 1.f) return;  // nothing to fill

  LONG x = static_cast<LONG>(g_boxFrac * static_cast<float>(bbW));
  if (x < 0) x = 0;
  if (static_cast<unsigned>(x) >= bbW) return;

  // The frame is fully drawn by now (this runs before the real Present), so a
  // rect Clear paints over everything that leaked into the margin — the old
  // screen's background, dialogs, toolbars, all of it.
  const D3DRECT rc = {x, 0, static_cast<LONG>(bbW), static_cast<LONG>(bbH)};
  const D3DCOLOR fill = 0xFF000000u | GetConfig().superUltrawideFill;
  const HRESULT hr = dev->Clear(1, &rc, D3DCLEAR_TARGET, fill, 0.f, 0);
  if (!g_loggedFill) {
    g_loggedFill = true;
    if (SUCCEEDED(hr))
      LOG("superwide: pillarbox margin filled — x %ld..%u of %ux%u, colour "
          "#%06X",
          x, bbW, bbW, bbH, GetConfig().superUltrawideFill);
    else
      LOG("superwide: pillarbox margin fill failed (Clear hr=0x%08lX) — "
          "cosmetic only, everything else unaffected",
          hr);
  }
}

}  // namespace superwide
