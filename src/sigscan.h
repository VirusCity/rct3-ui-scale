// sigscan.* — minimal array-of-bytes (AOB) signature scanner over a module's
// executable sections. Used by source_patch.* to locate the UI-scale lever by
// byte pattern instead of a hardcoded address, so the mod survives game/Steam
// patches and ports to sibling builds without re-hardcoding (see CLAUDE.md +
// research/ghidra_notes.md "AOB / SIGNATURE port").
//
// A pattern is written as a space-separated hex string with "??" wildcards,
// e.g. "68 60 C3 56 01 8D 4F 04 C7 07 48 18 12 01" or, with wildcards,
// "64 A3 00 00 00 00 83 3D ?? ?? ?? ?? 00 74". Matching is done only inside
// sections flagged IMAGE_SCN_MEM_EXECUTE.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace sigscan {

// Max bytes in one compiled pattern. Our signatures are short; 64 is plenty.
constexpr int kMaxPattern = 64;

// A compiled pattern: bytes[i] is compared only where mask[i] is true; mask[i]
// false marks a "??" wildcard position.
struct Pattern {
  uint8_t bytes[kMaxPattern] = {};
  bool mask[kMaxPattern] = {};
  int len = 0;
};

// Parse a "AA BB ?? CC" string into `out`. Returns false on a malformed token
// or if the pattern exceeds kMaxPattern. A pattern that is all-wildcard or empty
// is rejected (it would match everything / nothing useful).
bool Compile(const char* text, Pattern& out);

// Scan every executable section of `module` for `p`, storing up to `maxOut`
// match addresses in `out`. Returns the TOTAL number of matches found (which may
// exceed maxOut — only the first maxOut are stored). Use when a pattern is
// expected to match a small known set (e.g. both twin creators).
int FindAll(HMODULE module, const Pattern& p, uint8_t** out, int maxOut);

// Scan every executable section of `module` for `p`. Returns the sole match, or
// nullptr if there are zero OR more than one matches — ambiguity is refused so a
// signature that became non-unique on some build can never patch the wrong site.
// If `count` is non-null it receives the total number of matches found.
uint8_t* FindUnique(HMODULE module, const Pattern& p, int* count = nullptr);

// Overwrite 4 wildcard bytes at `byteOffset` in `p` with `value` (little-endian)
// and mark them fixed — used to bake a runtime-resolved absolute address into a
// pattern template. Returns false if the range doesn't fit.
bool SetDwordOperand(Pattern& p, int byteOffset, uint32_t value);

// Walk BACKWARD from `from` (inclusive) up to `maxBack` bytes, returning the
// first address at which `p` matches — used to find a function prologue from a
// distinctive body anchor. Returns nullptr if not found in range.
uint8_t* FindBackward(const uint8_t* from, size_t maxBack, const Pattern& p);

// Read a little-endian uint32 located `offset` bytes into a match (e.g. the
// disp32 absolute-address operand inside a matched instruction).
uint32_t ReadDwordAt(const uint8_t* match, int offset);

}  // namespace sigscan
