#include "scale.h"

#include "config.h"
#include "log.h"

namespace scale {
namespace {

// Log only when the inputs change — Uniform() is called per validation.
unsigned g_loggedW = 0, g_loggedH = 0;

}  // namespace

float Uniform(unsigned bbWidth, unsigned bbHeight) {
  if (!bbWidth || !bbHeight) return 1.f;

  const Config& cfg = GetConfig();
  float s;
  if (cfg.scaleOverride > 0.f) {
    s = cfg.scaleOverride;
  } else {
    const float sx =
        static_cast<float>(bbWidth) / static_cast<float>(cfg.referenceWidth);
    const float sy =
        static_cast<float>(bbHeight) / static_cast<float>(cfg.referenceHeight);
    s = sx < sy ? sx : sy;  // uniform: preserve the device aspect
  }
  if (s < 1.f) s = 1.f;  // never shrink the UI below native

  if (bbWidth != g_loggedW || bbHeight != g_loggedH) {
    g_loggedW = bbWidth;
    g_loggedH = bbHeight;
    LOG("scale: uniform S=%.4f for backbuffer %ux%u (%s)", s, bbWidth,
        bbHeight, cfg.scaleOverride > 0.f ? "user override" : "auto");
  }
  return s;
}

}  // namespace scale
