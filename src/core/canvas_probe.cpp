#include "canvas_probe.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "scale.h"

namespace probe {
namespace {

// The rect must sit at the canvas origin and describe a plausible pixel
// surface; these bounds are sanity rails, not layout assumptions.
constexpr float kOriginTol = 0.5f;
constexpr float kDimTol = 0.5f;
constexpr float kScaledTol = 1.5f;
constexpr float kMinDim = 64.f;
constexpr float kMaxDim = 32768.f;

uintptr_t g_imageBase = 0;
uintptr_t g_imageEnd = 0;

void ResolveImage() {
  if (g_imageBase) return;
  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) return;
  const auto base = reinterpret_cast<uintptr_t>(exe);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      base + dos->e_lfanew);
  g_imageEnd = base + nt->OptionalHeader.SizeOfImage;
  g_imageBase = base;
}

bool ReadableProt(DWORD prot) {
  const DWORD p = prot & 0xFF;
  return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
         p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE ||
         p == PAGE_EXECUTE_WRITECOPY;
}

}  // namespace

bool Readable(uintptr_t p, size_t len) {
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi)) !=
      sizeof(mbi))
    return false;
  if (mbi.State != MEM_COMMIT) return false;
  if ((mbi.Protect & PAGE_GUARD) || !ReadableProt(mbi.Protect)) return false;
  const uintptr_t regEnd =
      reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
  return p + len <= regEnd;
}

bool SafeRead(uintptr_t p, void* out, size_t len) {
  if (!Readable(p, len)) return false;
  __try {  // guards the race between VirtualQuery and the copy
    memcpy(out, reinterpret_cast<const void*>(p), len);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

uintptr_t ImageBase() {
  ResolveImage();
  return g_imageBase;
}

uintptr_t ImageEnd() {
  ResolveImage();
  return g_imageEnd;
}

Resolved Resolve(uint32_t slotRva, uint32_t rectOff, unsigned bbW,
                 unsigned bbH) {
  Resolved out{RectStatus::Unreadable, 0, 0, 0, 0, 0};
  ResolveImage();
  if (!g_imageBase || !slotRva || !bbW || !bbH) return out;

  // canvas = *(global slot) — fresh every call, never cached.
  uintptr_t canvas = 0;
  if (!SafeRead(g_imageBase + slotRva, &canvas, sizeof(canvas))) return out;
  if (!canvas) {
    out.status = RectStatus::Dormant;
    return out;
  }
  // The canvas is a heap object: aligned, outside the image.
  if ((canvas & 3) || (canvas >= g_imageBase && canvas < g_imageEnd) ||
      canvas < 0x10000)
    return out;

  float r[4];
  if (!SafeRead(canvas + rectOff, r, sizeof(r))) return out;
  out.rectVA = canvas + rectOff;
  out.l = r[0];
  out.t = r[1];
  out.r = r[2];
  out.b = r[3];

  const float w = r[2] - r[0];
  const float h = r[3] - r[1];
  if (std::fabs(r[0]) > kOriginTol || std::fabs(r[1]) > kOriginTol ||
      !(w > kMinDim && w < kMaxDim && h > kMinDim && h < kMaxDim)) {
    out.status = RectStatus::Mismatch;
    return out;
  }

  if (std::fabs(w - static_cast<float>(bbW)) <= kDimTol &&
      std::fabs(h - static_cast<float>(bbH)) <= kDimTol) {
    out.status = RectStatus::Unscaled;
    return out;
  }

  const float s = scale::Uniform(bbW, bbH);
  if (s > 1.001f &&
      std::fabs(w - static_cast<float>(bbW) / s) <= kScaledTol &&
      std::fabs(h - static_cast<float>(bbH) / s) <= kScaledTol) {
    out.status = RectStatus::Scaled;
    return out;
  }

  out.status = RectStatus::Mismatch;
  return out;
}

}  // namespace probe
