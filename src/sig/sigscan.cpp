#include "sigscan.h"

#include <cstring>

namespace sigscan {
namespace {

// Parse one hex nibble. Returns -1 on a non-hex char.
int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// True if `p` matches the `p.len` bytes at `at` (mask=false => wildcard).
bool MatchAt(const uint8_t* at, const Pattern& p) {
  for (int i = 0; i < p.len; ++i)
    if (p.mask[i] && at[i] != p.bytes[i]) return false;
  return true;
}

}  // namespace

bool Compile(const char* text, Pattern& out) {
  out.len = 0;
  bool anyFixed = false;
  for (const char* s = text; *s;) {
    while (*s == ' ' || *s == '\t') ++s;
    if (!*s) break;
    if (out.len >= kMaxPattern) return false;  // pattern too long

    if (s[0] == '?') {
      // "?" or "??" — a wildcard byte.
      out.bytes[out.len] = 0;
      out.mask[out.len] = false;
      ++out.len;
      ++s;
      if (*s == '?') ++s;
      continue;
    }

    const int hi = HexVal(s[0]);
    const int lo = HexVal(s[1]);
    if (hi < 0 || lo < 0) return false;  // malformed token
    out.bytes[out.len] = static_cast<uint8_t>((hi << 4) | lo);
    out.mask[out.len] = true;
    ++out.len;
    anyFixed = true;
    s += 2;
  }
  return out.len > 0 && anyFixed;  // reject empty / all-wildcard patterns
}

int FindAll(HMODULE module, const Pattern& p, uint8_t** out, int maxOut) {
  if (!module || p.len <= 0) return 0;

  auto* base = reinterpret_cast<uint8_t*>(module);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

  int n = 0;
  auto* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;  // code only
    uint8_t* start = base + sec->VirtualAddress;
    DWORD size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
    if (static_cast<int>(size) < p.len) continue;
    uint8_t* end = start + size - p.len;  // last position a full match can start
    for (uint8_t* q = start; q <= end; ++q) {
      if (!MatchAt(q, p)) continue;
      if (out && n < maxOut) out[n] = q;  // store the first maxOut; keep counting
      ++n;
    }
  }
  return n;
}

uint8_t* FindUnique(HMODULE module, const Pattern& p, int* count) {
  uint8_t* hits[2] = {nullptr, nullptr};
  const int n = FindAll(module, p, hits, 2);
  if (count) *count = n;
  return n == 1 ? hits[0] : nullptr;  // refuse ambiguity (0 or >1)
}

bool SetDwordOperand(Pattern& p, int byteOffset, uint32_t value) {
  if (byteOffset < 0 || byteOffset + 4 > p.len) return false;
  for (int i = 0; i < 4; ++i) {
    p.bytes[byteOffset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    p.mask[byteOffset + i] = true;
  }
  return true;
}

uint8_t* FindBackward(const uint8_t* from, size_t maxBack, const Pattern& p) {
  if (!from || p.len <= 0) return nullptr;
  for (size_t back = 0; back <= maxBack; ++back) {
    const uint8_t* q = from - back;
    if (MatchAt(q, p)) return const_cast<uint8_t*>(q);
  }
  return nullptr;
}

uint32_t ReadDwordAt(const uint8_t* match, int offset) {
  uint32_t v = 0;
  std::memcpy(&v, match + offset, 4);
  return v;
}

}  // namespace sigscan
