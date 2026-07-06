#include "config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

Config g_config;

float GetIniFloat(const char* ini, const char* sec, const char* key,
                  float def) {
  char buf[64] = {};
  char defBuf[64];
  sprintf_s(defBuf, "%f", def);
  GetPrivateProfileStringA(sec, key, defBuf, buf, sizeof(buf), ini);
  return static_cast<float>(atof(buf));
}

}  // namespace

bool LoadConfig(const char* iniPath) {
  Config& c = g_config;
  // --- user-facing settings (the shipped ini documents only these) --------
  c.enabled = GetPrivateProfileIntA("Master", "Enabled", 1, iniPath) != 0;
  c.scaleOverride = GetIniFloat(iniPath, "Features", "Scale", 0.f);
  c.borderless =
      GetPrivateProfileIntA("Features", "Borderless", 1, iniPath) != 0;
  c.cacheEnabled = GetPrivateProfileIntA("Debug", "Cache", 1, iniPath) != 0;
  c.logEnabled = GetPrivateProfileIntA("Debug", "Logging", 0, iniPath) != 0;
  c.logVerbose = GetPrivateProfileIntA("Debug", "Verbose", 0, iniPath) != 0;

  // --- advanced settings (undocumented; defaults are right for all known
  // editions — re-add the keys below only when porting/diagnosing) ---------
  c.apply = GetPrivateProfileIntA("Scaling", "Apply", 1, iniPath) != 0;
  c.referenceWidth =
      GetPrivateProfileIntA("Scaling", "ReferenceWidth", 1920, iniPath);
  c.referenceHeight =
      GetPrivateProfileIntA("Scaling", "ReferenceHeight", 1080, iniPath);
  c.borderlessWidth =
      GetPrivateProfileIntA("Borderless", "Width", 0, iniPath);
  c.borderlessHeight =
      GetPrivateProfileIntA("Borderless", "Height", 0, iniPath);
  if (c.borderlessWidth < 0) c.borderlessWidth = 0;
  if (c.borderlessHeight < 0) c.borderlessHeight = 0;

  c.stableFrames = GetPrivateProfileIntA("Gate", "StableFrames", 30, iniPath);
  c.stableFramesPlaceholder =
      GetPrivateProfileIntA("Gate", "StableFramesPlaceholder", 120, iniPath);

  c.discStableFrames =
      GetPrivateProfileIntA("Discovery", "StableFrames", 60, iniPath);
  c.discTimeoutFrames =
      GetPrivateProfileIntA("Discovery", "TimeoutFrames", 900, iniPath);
  c.slotsPerTick =
      GetPrivateProfileIntA("Discovery", "SlotsPerTick", 65536, iniPath);
  c.maxRectOffset =
      GetPrivateProfileIntA("Discovery", "MaxRectOffset", 0x400, iniPath);
  c.minCanvasXref =
      GetPrivateProfileIntA("Discovery", "MinCanvasXref", 40, iniPath);

  GetPrivateProfileStringA("Signatures", "GuardA", "", c.sigGuardA,
                           sizeof(c.sigGuardA), iniPath);
  GetPrivateProfileStringA("Signatures", "PrologueA", "", c.sigPrologueA,
                           sizeof(c.sigPrologueA), iniPath);
  GetPrivateProfileStringA("Signatures", "GuardB", "", c.sigGuardB,
                           sizeof(c.sigGuardB), iniPath);
  GetPrivateProfileStringA("Signatures", "PrologueB", "", c.sigPrologueB,
                           sizeof(c.sigPrologueB), iniPath);

  // Sanity clamps — a nonsense reference dimension would corrupt every scale
  // computation downstream.
  if (c.referenceWidth < 320) c.referenceWidth = 1920;
  if (c.referenceHeight < 240) c.referenceHeight = 1080;
  if (c.scaleOverride < 0.f || c.scaleOverride > 8.f) c.scaleOverride = 0.f;
  if (c.stableFrames < 5) c.stableFrames = 5;
  if (c.stableFramesPlaceholder < c.stableFrames)
    c.stableFramesPlaceholder = c.stableFrames;
  if (c.discStableFrames < 5) c.discStableFrames = 5;
  if (c.discTimeoutFrames < c.discStableFrames * 2)
    c.discTimeoutFrames = c.discStableFrames * 2;
  if (c.slotsPerTick < 4096) c.slotsPerTick = 4096;
  if (c.maxRectOffset < 0x20) c.maxRectOffset = 0x20;
  if (c.maxRectOffset > 0x400) c.maxRectOffset = 0x400;  // probe buffer bound
  return true;
}

const Config& GetConfig() { return g_config; }
