#include "source_patch.h"

#include <windows.h>

#include <cstdint>

#include "logging.h"

namespace sourcepatch {
namespace {

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
