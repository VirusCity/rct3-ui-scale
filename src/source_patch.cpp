#include "source_patch.h"

#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "config.h"
#include "logging.h"
#include "sigscan.h"

namespace sourcepatch {
namespace {

// --- UI-scale lever: the GUI2 reference canvas (RCTDesktop) -----------------
// The three build-specific values below are resolved at runtime by SIGNATURE
// (see ResolveLeverSites + research/ghidra_notes.md "AOB / SIGNATURE port"), so
// the mod survives game/Steam patches and sibling builds. The baked constants
// here are the FALLBACK for this exact Steam build (RCT3.exe, image base
// 0x400000, no ASLR) — used only if a scan fails or turns up ambiguous, so the
// mod is strictly more robust than the hardcoded version, never less.
//   FUN_00a24a70  = the RCTDesktop creator used in normal play (alloc + ctor +
//                   set canvas rect). FUN_007b42d0 is an identical alternate
//                   path; we hook both — only one runs (singleton-guarded).
//   0x012e4fa4    = global RCTDesktop* (the singleton "s_desktop").
//   desktop+0x7c  = canvas rect as 4 floats: [0]=left [1]=top [2]=right [3]=bottom.
constexpr uintptr_t kFallbackCreateMain = 0x00a24a70;  // real path (confirmed live)
constexpr uintptr_t kFallbackCreateAlt = 0x007b42d0;   // identical twin path
constexpr uintptr_t kFallbackGlobalPtr = 0x012e4fa4;
constexpr unsigned kCanvasRectOff = 0x7c;

// Signatures (see ghidra_notes.md). Both creators share a generic MSVC SEH
// prologue, so we anchor on instructions that reference the s_desktop global and
// walk back to the prologue. The global address is recovered first (from the
// guard) and baked into the creator-store template at runtime.
//   kSigAltGuard   : mov fs:[0],eax; cmp dword[<global>],0; jz — uniquely the alt
//                    creator's frame-install + singleton guard. The 4 wildcard
//                    bytes at offset +8 ARE the s_desktop global pointer. (The
//                    main creator has a guard too, but not in this fs:[0] context.)
//   kSigCreatorSt  : mov ecx,[<adjacent singleton>]; mov [<s_desktop>],eax — the
//                    global-WRITE both creators perform (byte-identical twins; the
//                    destructor uses an immediate store, so it's excluded). Matches
//                    exactly the two creators. The dword at offset +7 is patched
//                    with the resolved s_desktop address at runtime.
//   kSigPrologue   : push ebp; mov ebp,esp; push -1; push <handler> — function entry.
constexpr const char* kSigAltGuard = "64 A3 00 00 00 00 83 3D ?? ?? ?? ?? 00 74";
constexpr int kAltGuardGlobalOff = 8;  // disp32 offset within kSigAltGuard
constexpr const char* kSigCreatorStore = "8B 0D ?? ?? ?? ?? A3 ?? ?? ?? ??";
constexpr int kCreatorStoreGlobalOff = 7;  // disp32 of the [s_desktop] store target
constexpr const char* kSigPrologue = "55 8B EC 6A FF 68";
constexpr size_t kMaxPrologueBack = 0x80;   // guard sits <0x40 past the alt entry
constexpr size_t kMaxStoreBack = 0x280;     // the global-store sits up to ~0x1A0 in

// Resolved at InstallUiScaleHook(); fall back to the baked constants on failure.
uintptr_t g_createMain = 0;
uintptr_t g_createAlt = 0;
uintptr_t g_desktopGlobalPtr = 0;
// Canvas-rect offset within the RCTDesktop object. Defaults to the Steam-build
// value; an INI override ([Signatures] CanvasRectOffset) can replace it.
unsigned g_canvasRectOff = kCanvasRectOff;

using CreateDesktopFn = void(__cdecl*)();
CreateDesktopFn g_origCreateMain = nullptr;
CreateDesktopFn g_origCreateAlt = nullptr;
float g_uiScale = 1.0f;

// After the game builds the RCTDesktop (canvas = full device resolution), shrink
// the canvas by g_uiScale. A smaller canvas mapped to the same device => the
// whole UI is laid out larger; the hit-test reads the same canvas so clicks
// stay aligned. Done right after creation (before the toolbars/panels lay out)
// so edge-anchored elements reposition correctly instead of overflowing.
void ScaleDesktopCanvas(const char* via) {
  uintptr_t desktop = *reinterpret_cast<uintptr_t*>(g_desktopGlobalPtr);
  if (!desktop) {
    LOG("uiscale[desktop]: %s ran but s_desktop is null — not scaling.", via);
    return;
  }
  float* rect = reinterpret_cast<float*>(desktop + g_canvasRectOff);
  const float w = rect[2] - rect[0];
  const float h = rect[3] - rect[1];
  // Sanity: only touch a plausible pixel canvas, so a wrong address can't corrupt.
  if (!(w > 64.0f && w < 32768.0f && h > 64.0f && h < 32768.0f)) {
    LOG("uiscale[desktop]: canvas %.1fx%.1f implausible — skipping (wrong build?).", w, h);
    return;
  }
  rect[2] = rect[0] + w / g_uiScale;
  rect[3] = rect[1] + h / g_uiScale;
  LOG("uiscale[desktop]: (%s) canvas %.0fx%.0f -> %.0fx%.0f (UiScale=%.3f); UI will "
      "lay out %.2fx larger.",
      via, w, h, rect[2] - rect[0], rect[3] - rect[1], g_uiScale, g_uiScale);
}

void __cdecl DetourCreateMain() {
  g_origCreateMain();
  ScaleDesktopCanvas("FUN_00a24a70");
}
void __cdecl DetourCreateAlt() {
  g_origCreateAlt();
  ScaleDesktopCanvas("FUN_007b42d0");
}

// One decoded candidate: a `mov dword ptr [reg+0xF0], 1.0f` instruction.
// `immAddr` points at the 4-byte imm32 we rewrite; `mov` is the opcode start.
struct Site {
  uint8_t* mov;
  uint8_t* immAddr;
};

// Does the 10 bytes at p encode `mov dword ptr [reg + 0xF0], 0x3F800000`?
//   C7 /0  ModRM(mod=10, reg=000, rm=base)  disp32=0x000000F0  imm32=1.0f
// Base register is encoded in ModRM.rm (EAX..EDI); rm==4 (ESP) would need a SIB
// byte and shift everything, so we exclude it. Matching the full instruction —
// opcode, the exact +0xF0 displacement, AND the 1.0f immediate — makes a false
// positive in executable bytes effectively impossible (confirmed: 12 hits, all
// in GUI2 element ctors, matching the headless Ghidra scan in ghidra_notes.md).
bool IsScaleDefaultMov(const uint8_t* p) {
  if (p[0] != 0xC7) return false;                  // MOV r/m32, imm32
  const uint8_t modrm = p[1];
  if ((modrm & 0xC0) != 0x80) return false;        // mod=10 -> [reg + disp32]
  if ((modrm & 0x38) != 0x00) return false;        // reg field /0 (MOV)
  if ((modrm & 0x07) == 0x04) return false;        // rm=ESP would mean a SIB byte
  // disp32 == 0x000000F0
  if (p[2] != 0xF0 || p[3] != 0x00 || p[4] != 0x00 || p[5] != 0x00) return false;
  // imm32 == 0x3F800000 (1.0f)
  if (p[6] != 0x00 || p[7] != 0x00 || p[8] != 0x80 || p[9] != 0x3F) return false;
  return true;
}

// Overwrite 4 bytes of executable code, flipping page protection around it.
bool WriteImm32(uint8_t* at, uint32_t value) {
  DWORD oldProt = 0;
  if (!VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
  memcpy(at, &value, 4);
  DWORD tmp = 0;
  VirtualProtect(at, 4, oldProt, &tmp);
  FlushInstructionCache(GetCurrentProcess(), at, 4);
  return true;
}

}  // namespace

namespace {
// Create + enable one creator hook. Returns true on success.
bool ArmCreatorHook(uintptr_t addr, void* detour, void** orig) {
  void* target = reinterpret_cast<void*>(addr);
  if (MH_CreateHook(target, detour, orig) != MH_OK) {
    LOG("uiscale[desktop]: MH_CreateHook(0x%IX) failed.", addr);
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    LOG("uiscale[desktop]: MH_EnableHook(0x%IX) failed.", addr);
    return false;
  }
  return true;
}

// The signatures actually used at runtime: an INI override ([Signatures]) when
// the user supplied one, else the built-in pattern. The returned const char*s
// point into the process-global Config (it outlives every caller), so they are
// safe to hold. Both source_patch and the discovery pass share this.
struct EffectiveSigs {
  const char* altGuard;
  int altGuardOff;
  const char* store;
  int storeOff;
  const char* prologue;
};
EffectiveSigs GetEffectiveSigs() {
  const Config& c = GetConfig();
  EffectiveSigs e;
  e.altGuard = c.sigAltGuard.empty() ? kSigAltGuard : c.sigAltGuard.c_str();
  e.altGuardOff =
      c.sigAltGuardGlobalOff >= 0 ? c.sigAltGuardGlobalOff : kAltGuardGlobalOff;
  e.store = c.sigCreatorStore.empty() ? kSigCreatorStore : c.sigCreatorStore.c_str();
  e.storeOff = c.sigCreatorStoreGlobalOff >= 0 ? c.sigCreatorStoreGlobalOff
                                               : kCreatorStoreGlobalOff;
  e.prologue = c.sigPrologue.empty() ? kSigPrologue : c.sigPrologue.c_str();
  return e;
}

// End of the mapped exe image, for bounds-checking recovered global pointers.
uintptr_t ImageEnd(uintptr_t imgBase) {
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(imgBase);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(imgBase + dos->e_lfanew);
  return imgBase + nt->OptionalHeader.SizeOfImage;
}

// Resolve the three build-specific lever sites. Priority PER VALUE is:
//   INI RVA override > INI signature > built-in signature > baked Steam fallback.
// Every key in [Signatures] defaults to unset, so with no INI overrides this
// behaves EXACTLY as the signature-only resolver did (stock Complete unaffected).
// Logs the source of each value so a blind log shows what is in effect.
void ResolveLeverSites() {
  g_createMain = kFallbackCreateMain;
  g_createAlt = kFallbackCreateAlt;
  g_desktopGlobalPtr = kFallbackGlobalPtr;

  const Config& cfg = GetConfig();
  if (cfg.ovrCanvasRectOff) {
    g_canvasRectOff = cfg.ovrCanvasRectOff;
    LOG("uiscale[desktop]: canvas-rect offset 0x%X (INI).", g_canvasRectOff);
  }

  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) {
    LOG("uiscale[desktop]: GetModuleHandle(exe) failed — using baked addresses.");
    return;
  }
  const auto imgBase = reinterpret_cast<uintptr_t>(exe);
  const uintptr_t imgEnd = ImageEnd(imgBase);
  const EffectiveSigs sig = GetEffectiveSigs();

  sigscan::Pattern altGuard, creatorStore, prologue;
  const bool compiled = sigscan::Compile(sig.altGuard, altGuard) &&
                        sigscan::Compile(sig.store, creatorStore) &&
                        sigscan::Compile(sig.prologue, prologue);
  if (!compiled)
    LOG("uiscale[desktop]: signature compile failed — sig tier disabled, using "
        "INI RVAs / baked addresses only.");

  // (1) Singleton guard -> the s_desktop global pointer + the alt creator entry.
  uintptr_t altEntry = 0;
  if (compiled) {
    int n = 0;
    if (uint8_t* g = sigscan::FindUnique(exe, altGuard, &n)) {
      const uint32_t glob = sigscan::ReadDwordAt(g, sig.altGuardOff);
      // Sanity: the global must land inside the exe image (its writable data), or
      // a mis-decode could hand ScaleDesktopCanvas a wild pointer to dereference.
      if (glob > imgBase && glob < imgEnd) {
        g_desktopGlobalPtr = glob;
        LOG("uiscale[desktop]: s_desktop global @ 0x%X (sig).", glob);
      } else {
        LOG("uiscale[desktop]: guard global 0x%X out of image — fallback 0x%IX.",
            glob, g_desktopGlobalPtr);
      }
      if (uint8_t* entry = sigscan::FindBackward(g, kMaxPrologueBack, prologue)) {
        altEntry = reinterpret_cast<uintptr_t>(entry);
        g_createAlt = altEntry;
        LOG("uiscale[desktop]: alt creator @ exe+0x%IX (sig).", g_createAlt - imgBase);
      } else {
        LOG("uiscale[desktop]: alt guard matched but no prologue — fallback exe+0x%IX.",
            g_createAlt - imgBase);
      }
    } else {
      LOG("uiscale[desktop]: alt guard sig %d match(es) — fallback alt exe+0x%IX, "
          "global 0x%IX.", n, g_createAlt - imgBase, g_desktopGlobalPtr);
    }
  }

  // INI RVA override for the global wins over whatever the signature resolved.
  if (cfg.ovrDesktopGlobalRVA) {
    g_desktopGlobalPtr = imgBase + cfg.ovrDesktopGlobalRVA;
    LOG("uiscale[desktop]: s_desktop global @ 0x%IX (INI RVA 0x%X).",
        g_desktopGlobalPtr, cfg.ovrDesktopGlobalRVA);
  }

  // (2) Main creator: the global-WRITE matches both twin creators; the one that
  // isn't the alt is the main. Bake the (possibly overridden) global into the
  // store template, find every write site, walk each back to its prologue, take
  // the non-alt. Uses whatever alt resolved to (baked fallback if the guard missed).
  if (!altEntry) altEntry = g_createAlt;
  if (compiled &&
      sigscan::SetDwordOperand(creatorStore, sig.storeOff,
                               static_cast<uint32_t>(g_desktopGlobalPtr))) {
    uint8_t* hits[8] = {};
    const int sc = sigscan::FindAll(exe, creatorStore, hits, 8);
    uintptr_t mainEntry = 0;
    int nonAlt = 0;
    for (int i = 0; i < sc && i < 8; ++i) {
      uint8_t* e = sigscan::FindBackward(hits[i], kMaxStoreBack, prologue);
      if (!e) continue;
      const uintptr_t ev = reinterpret_cast<uintptr_t>(e);
      if (ev == altEntry) continue;  // the alt is already accounted for
      ++nonAlt;
      mainEntry = ev;
    }
    if (nonAlt == 1 && mainEntry) {
      g_createMain = mainEntry;
      LOG("uiscale[desktop]: main creator @ exe+0x%IX (sig, %d store site(s)).",
          g_createMain - imgBase, sc);
    } else {
      LOG("uiscale[desktop]: creator-store sites=%d, non-alt candidates=%d — "
          "fallback exe+0x%IX.", sc, nonAlt, g_createMain - imgBase);
    }
  }

  // INI RVA overrides for the creators win last (most reliable for porters).
  if (cfg.ovrCreateAltRVA) {
    g_createAlt = imgBase + cfg.ovrCreateAltRVA;
    LOG("uiscale[desktop]: alt creator @ exe+0x%X (INI RVA).", cfg.ovrCreateAltRVA);
  }
  if (cfg.ovrCreateMainRVA) {
    g_createMain = imgBase + cfg.ovrCreateMainRVA;
    LOG("uiscale[desktop]: main creator @ exe+0x%X (INI RVA).", cfg.ovrCreateMainRVA);
  }
}
}  // namespace

// ---------------------------------------------------------------------------
//  [Diagnostics] DiscoverSignatures — read-only porting aid.
// ---------------------------------------------------------------------------
// On an edition where the built-in resolver fell back to the (wrong) Steam
// addresses, this behaviorally identifies the real s_desktop global + canvas
// offset by matching a candidate object's canvas rect against the LIVE display
// resolution, then logs a ready-to-paste [Signatures] block. Every memory read
// is VirtualQuery-guarded, so a wrong candidate can never fault. It never alters
// the hook path — pure reporting — and self-disables once it reports or gives up.
namespace {

bool g_discoverDone = false;  // once set, DiscoveryActive() is false: no per-frame cost.
int g_discoverFrames = 0;

struct GuardCand {
  uint32_t globVA;          // candidate s_desktop global pointer (absolute VA)
  const uint8_t* guardHit;  // the guard match, for walking back to the alt creator
};
GuardCand g_cands[8];
int g_candN = -1;  // -1 = not scanned yet; 0 = scanned, none usable.

inline float AbsF(float x) { return x < 0.0f ? -x : x; }

// True iff [p, p+len) is committed, readable, and inside a single region — then
// the bytes are copied to `out`. Lets us probe heap pointers without faulting.
bool SafeRead(const void* p, void* out, size_t len) {
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
  if (mbi.State != MEM_COMMIT) return false;
  const DWORD prot = mbi.Protect & 0xFF;
  const bool readable = prot == PAGE_READONLY || prot == PAGE_READWRITE ||
                        prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ ||
                        prot == PAGE_EXECUTE_READWRITE ||
                        prot == PAGE_EXECUTE_WRITECOPY;
  if (!readable || (mbi.Protect & PAGE_GUARD)) return false;
  const auto* regEnd =
      static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
  if (static_cast<const uint8_t*>(p) + len > regEnd) return false;
  memcpy(out, p, len);
  return true;
}

// Scan the first 0x200 bytes of the object for a canvas rect (L,T,R,B floats,
// L/T ~ 0, W/H matching the display). Accepts both the unscaled canvas (== device
// resolution) and one already shrunk by g_uiScale, so the probe works whether or
// not a correct creator hook has run this frame. Returns the offset + measured W/H.
bool FindCanvasRect(uintptr_t obj, int devW, int devH, unsigned* offOut,
                    float* wOut, float* hOut) {
  uint8_t buf[0x200];
  if (!SafeRead(reinterpret_cast<const void*>(obj), buf, sizeof(buf)))
    return false;
  for (unsigned off = 0x10; off + 16 <= sizeof(buf); off += 4) {
    float r[4];
    memcpy(r, buf + off, sizeof(r));
    const float w = r[2] - r[0], h = r[3] - r[1];
    if (AbsF(r[0]) > 1.0f || AbsF(r[1]) > 1.0f) continue;       // L,T ~ 0
    if (!(w > 64.0f && w < 32768.0f && h > 64.0f && h < 32768.0f)) continue;
    const float tol = 1.5f;
    const bool unscaled = AbsF(w - devW) < tol && AbsF(h - devH) < tol;
    const bool scaled = g_uiScale > 1.0f && AbsF(w - devW / g_uiScale) < tol &&
                        AbsF(h - devH / g_uiScale) < tol;
    if (unscaled || scaled) {
      *offOut = off;
      *wOut = w;
      *hOut = h;
      return true;
    }
  }
  return false;
}

// One-time: scan the exe for guard matches and cache the in-image global
// candidates, so the per-frame probe only re-derefs a handful of pointers.
void BuildCandidates(HMODULE exe, uintptr_t imgBase, uintptr_t imgEnd,
                     const EffectiveSigs& sig) {
  g_candN = 0;
  sigscan::Pattern guard;
  if (!sigscan::Compile(sig.altGuard, guard)) {
    LOG("discover: guard signature failed to compile — cannot scan this build.");
    return;
  }
  uint8_t* gm[8] = {};
  const int gn = sigscan::FindAll(exe, guard, gm, 8);
  for (int i = 0; i < gn && i < 8 && g_candN < 8; ++i) {
    const uint32_t globVA = sigscan::ReadDwordAt(gm[i], sig.altGuardOff);
    if (globVA > imgBase && globVA < imgEnd) {
      g_cands[g_candN].globVA = globVA;
      g_cands[g_candN].guardHit = gm[i];
      ++g_candN;
    }
  }
  LOG("discover: armed — %d candidate s_desktop global(s) from %d guard match(es). "
      "Open a park so the UI exists, then watch for the CONFIRMED line.", g_candN, gn);
}

// A candidate validated: recover the creator RVAs from the validated global and
// log the paste-ready [Signatures] block.
void ReportConfirmed(HMODULE exe, uintptr_t imgBase, const EffectiveSigs& sig,
                     const GuardCand& cand, uintptr_t obj, unsigned canvasOff,
                     float w, float h, int devW, int devH) {
  uintptr_t altVA = 0, mainVA = 0;
  sigscan::Pattern prologue, store;
  const bool okP = sigscan::Compile(sig.prologue, prologue);
  if (okP) {
    if (uint8_t* e = sigscan::FindBackward(cand.guardHit, kMaxPrologueBack, prologue))
      altVA = reinterpret_cast<uintptr_t>(e);
  }
  if (okP && sigscan::Compile(sig.store, store) &&
      sigscan::SetDwordOperand(store, sig.storeOff, cand.globVA)) {
    uint8_t* sh[8] = {};
    const int sc = sigscan::FindAll(exe, store, sh, 8);
    for (int j = 0; j < sc && j < 8; ++j) {
      uint8_t* e = sigscan::FindBackward(sh[j], kMaxStoreBack, prologue);
      if (!e) continue;
      const uintptr_t ev = reinterpret_cast<uintptr_t>(e);
      if (ev != altVA) mainVA = ev;  // the non-alt store writer is the main creator
    }
  }

  LOG("discover: s_desktop 0x%X -> object %p, %.0fx%.0f canvas at +0x%X  "
      "*** CONFIRMED against %dx%d display ***",
      cand.globVA, (void*)obj, w, h, canvasOff, devW, devH);
  LOG("discover: paste this into d3d9_uiscale.ini, then restart the game:");
  LOG("discover:     [Signatures]");
  if (mainVA) LOG("discover:     CreateMainRVA    = 0x%08IX", mainVA - imgBase);
  if (altVA)  LOG("discover:     CreateAltRVA     = 0x%08IX", altVA - imgBase);
  LOG("discover:     DesktopGlobalRVA = 0x%08IX",
      static_cast<uintptr_t>(cand.globVA) - imgBase);
  LOG("discover:     CanvasRectOffset = 0x%X", canvasOff);
  if (!mainVA && !altVA)
    LOG("discover: NOTE — could not recover the creator RVAs by signature on this "
        "build; paste DesktopGlobalRVA + CanvasRectOffset and find the two creators "
        "in a debugger (functions that write s_desktop). The global+offset alone "
        "are the hard part and are now confirmed.");
}

}  // namespace

bool DiscoveryActive() {
  return GetConfig().discoverSignatures && !g_discoverDone;
}

void DiscoverProbe(int devW, int devH) {
  if (g_discoverDone || devW <= 0 || devH <= 0) return;

  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) {
    g_discoverDone = true;
    return;
  }
  const auto imgBase = reinterpret_cast<uintptr_t>(exe);
  const uintptr_t imgEnd = ImageEnd(imgBase);
  const EffectiveSigs sig = GetEffectiveSigs();

  if (g_candN < 0) BuildCandidates(exe, imgBase, imgEnd, sig);
  if (g_candN == 0) {
    LOG("discover: no usable s_desktop candidates — the structural signatures "
        "don't fit this build, so the log can't report sites; a debugger pass is "
        "required here.");
    g_discoverDone = true;
    return;
  }

  ++g_discoverFrames;
  for (int i = 0; i < g_candN; ++i) {
    uintptr_t obj = 0;
    if (!SafeRead(reinterpret_cast<const void*>(
                      static_cast<uintptr_t>(g_cands[i].globVA)),
                  &obj, sizeof(obj)))
      continue;
    if (!obj) continue;  // desktop not built yet this frame — try again next frame
    unsigned canvasOff = 0;
    float w = 0.0f, h = 0.0f;
    if (!FindCanvasRect(obj, devW, devH, &canvasOff, &w, &h)) continue;
    ReportConfirmed(exe, imgBase, sig, g_cands[i], obj, canvasOff, w, h, devW, devH);
    g_discoverDone = true;
    return;
  }

  // ~30s at 60fps. If no candidate ever held a display-sized canvas, the object
  // layout differs enough that behavioral validation can't lock the offset.
  if (g_discoverFrames >= 1800) {
    LOG("discover: no candidate validated in %d frames — UI canvas never matched "
        "the display; a debugger pass is needed for this build.", g_discoverFrames);
    g_discoverDone = true;
  }
}

bool InstallUiScaleHook(float uiScale) {
  if (uiScale <= 1.0f) {
    LOG("uiscale[desktop]: UiScale=%.3f (<= 1.0) — disabled.", uiScale);
    return false;
  }
  if (uiScale > 4.0f) uiScale = 4.0f;  // clamp; beyond this the UI is unusable
  g_uiScale = uiScale;

  // Locate the creators + the s_desktop global by signature (baked-address
  // fallback inside) before arming the hooks on whatever they resolved to.
  ResolveLeverSites();

  bool a = ArmCreatorHook(g_createMain, reinterpret_cast<void*>(&DetourCreateMain),
                          reinterpret_cast<void**>(&g_origCreateMain));
  bool b = ArmCreatorHook(g_createAlt, reinterpret_cast<void*>(&DetourCreateAlt),
                          reinterpret_cast<void**>(&g_origCreateAlt));
  LOG("uiscale[desktop]: hooks armed (main=%d alt=%d) on RCTDesktop creators, "
      "UiScale=%.3f.",
      a ? 1 : 0, b ? 1 : 0, uiScale);
  return a || b;
}

int ApplyGui2ScaleDefault(float scale) {
  if (scale == 1.0f) {
    LOG("sourcepatch: scale == 1.0, nothing to patch.");
    return 0;
  }

  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) {
    LOG("sourcepatch: GetModuleHandle(exe) failed — skipping.");
    return 0;
  }
  auto* base = reinterpret_cast<uint8_t*>(exe);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    LOG("sourcepatch: bad PE header — skipping.");
    return 0;
  }

  uint32_t immBits;
  memcpy(&immBits, &scale, 4);  // float -> raw bits for the patched immediate

  int patched = 0, found = 0;
  auto* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
    if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;  // code only
    uint8_t* start = base + sec->VirtualAddress;
    DWORD size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
    if (size < 10) continue;
    uint8_t* end = start + size - 10;  // need 10 bytes for the full instruction

    for (uint8_t* p = start; p <= end; ++p) {
      if (!IsScaleDefaultMov(p)) continue;
      ++found;
      uint8_t* immAddr = p + 6;
      uintptr_t rva = static_cast<uintptr_t>(p - base);
      if (WriteImm32(immAddr, immBits)) {
        ++patched;
        LOG("sourcepatch:   patched +0xF0 default 1.0 -> %.3f @ exe+0x%IX", scale,
            rva);
      } else {
        LOG("sourcepatch:   FAILED to write @ exe+0x%IX (VirtualProtect)", rva);
      }
    }
  }

  LOG("sourcepatch: GUI2 +0xF0 default scale: %d site(s) found, %d patched "
      "(scale=%.3f). If the UI grows with correct clicks, +0xF0 is the lever.",
      found, patched, scale);
  return patched;
}

}  // namespace sourcepatch
