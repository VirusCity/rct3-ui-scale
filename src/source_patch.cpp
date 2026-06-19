#include "source_patch.h"

#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "logging.h"

namespace sourcepatch {
namespace {

// --- UI-scale lever: the GUI2 reference canvas (RCTDesktop) -----------------
// Addresses for THIS Steam build (RCT3.exe, image base 0x400000, no ASLR), from
// our own reverse engineering (research/ghidra_notes.md):
//   FUN_00a24a70  = the RCTDesktop creator used in normal play (alloc + ctor +
//                   set canvas rect). FUN_007b42d0 is an identical alternate
//                   path; we hook both — only one runs (singleton-guarded).
//   0x012e4fa4    = global RCTDesktop* (the singleton "s_desktop").
//   desktop+0x7c  = canvas rect as 4 floats: [0]=left [1]=top [2]=right [3]=bottom.
constexpr uintptr_t kCreateDesktopMain = 0x00a24a70;  // real path (confirmed live)
constexpr uintptr_t kCreateDesktopAlt = 0x007b42d0;   // identical twin path
constexpr uintptr_t kDesktopGlobalPtr = 0x012e4fa4;
constexpr unsigned kCanvasRectOff = 0x7c;

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
  uintptr_t desktop = *reinterpret_cast<uintptr_t*>(kDesktopGlobalPtr);
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
}  // namespace

bool InstallUiScaleHook(float uiScale) {
  if (uiScale <= 1.0f) {
    LOG("uiscale[desktop]: UiScale=%.3f (<= 1.0) — disabled.", uiScale);
    return false;
  }
  if (uiScale > 4.0f) uiScale = 4.0f;  // clamp; beyond this the UI is unusable
  g_uiScale = uiScale;

  bool a = ArmCreatorHook(kCreateDesktopMain, reinterpret_cast<void*>(&DetourCreateMain),
                          reinterpret_cast<void**>(&g_origCreateMain));
  bool b = ArmCreatorHook(kCreateDesktopAlt, reinterpret_cast<void*>(&DetourCreateAlt),
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
