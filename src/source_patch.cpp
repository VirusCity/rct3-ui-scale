#include "source_patch.h"

#include <windows.h>

#include <cstdint>

#include "MinHook.h"
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
  float* rect = reinterpret_cast<float*>(desktop + kCanvasRectOff);
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

// Resolve the three build-specific lever sites by signature, falling back to the
// baked constants for this Steam build whenever a scan fails or is ambiguous.
// Logs the source of each value so a blind log shows whether AOB or the fallback
// is in effect. Always leaves g_* populated (with a usable address either way).
void ResolveLeverSites() {
  g_createMain = kFallbackCreateMain;
  g_createAlt = kFallbackCreateAlt;
  g_desktopGlobalPtr = kFallbackGlobalPtr;

  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) {
    LOG("uiscale[desktop]: GetModuleHandle(exe) failed — using baked addresses.");
    return;
  }
  const auto imgBase = reinterpret_cast<uintptr_t>(exe);

  sigscan::Pattern altGuard, creatorStore, prologue;
  if (!(sigscan::Compile(kSigAltGuard, altGuard) &&
        sigscan::Compile(kSigCreatorStore, creatorStore) &&
        sigscan::Compile(kSigPrologue, prologue))) {
    LOG("uiscale[desktop]: signature compile failed — using baked addresses.");
    return;
  }

  // (1) Singleton guard -> the s_desktop global pointer + the alt creator entry.
  uintptr_t altEntry = 0;
  int n = 0;
  if (uint8_t* g = sigscan::FindUnique(exe, altGuard, &n)) {
    const uint32_t glob = sigscan::ReadDwordAt(g, kAltGuardGlobalOff);
    // Sanity: the global must land inside the exe image (its writable data), or a
    // mis-decode could hand ScaleDesktopCanvas a wild pointer to dereference.
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        imgBase + reinterpret_cast<IMAGE_DOS_HEADER*>(imgBase)->e_lfanew);
    const uintptr_t imgEnd = imgBase + nt->OptionalHeader.SizeOfImage;
    if (glob > imgBase && glob < imgEnd) {
      g_desktopGlobalPtr = glob;
      LOG("uiscale[desktop]: s_desktop global @ 0x%X (sig).", glob);
    } else {
      LOG("uiscale[desktop]: guard global 0x%X out of image — fallback 0x%IX.", glob,
          g_desktopGlobalPtr);
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
  // Compare against whatever alt resolved to (baked fallback if the guard missed).
  if (!altEntry) altEntry = g_createAlt;

  // (2) Main creator: the global-WRITE matches both twin creators; the one that
  // isn't the alt is the main. Bake the resolved global into the store template,
  // find every write site, walk each back to its prologue, and take the non-alt.
  if (sigscan::SetDwordOperand(creatorStore, kCreatorStoreGlobalOff,
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
  } else {
    LOG("uiscale[desktop]: store template build failed — fallback exe+0x%IX.",
        g_createMain - imgBase);
  }
}
}  // namespace

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
