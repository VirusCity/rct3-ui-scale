#include "config.h"

#include <windows.h>

#include <cstdlib>

namespace {
Config g_config;  // holds defaults until LoadConfig() runs.

// The Win32 profile API is the simplest dependency-free INI reader and matches
// the .ini format players expect. All reads are by full path.
std::string ReadString(const char* section, const char* key,
                       const char* def, const std::string& ini) {
  char buf[512];
  GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), ini.c_str());
  return std::string(buf);
}
}  // namespace

void LoadConfig(const std::string& iniPath) {
  // [General] ScaleFactor=1.5
  std::string scaleStr = ReadString("General", "ScaleFactor", "1.5", iniPath);
  float parsed = static_cast<float>(atof(scaleStr.c_str()));
  if (parsed > 0.1f && parsed < 10.0f)  // sanity clamp; ignore garbage.
    g_config.scale = parsed;

  // [Diagnostics] LoggingEnabled=1  (moved here from [General]). For backward
  // compatibility with already-deployed .ini files we read the old [General]
  // location first and use it as the default, so an existing LoggingEnabled
  // setting keeps working even if the file hasn't been updated.
  int legacyLog =
      GetPrivateProfileIntA("General", "LoggingEnabled", 1, iniPath.c_str());
  g_config.logEnabled =
      GetPrivateProfileIntA("Diagnostics", "LoggingEnabled", legacyLog,
                            iniPath.c_str()) != 0;

  // [Diagnostics] LogPath=  (renamed from [Advanced] LogFile; blank => default
  // path). Same backward-compat fallback to the old [Advanced] LogFile key.
  std::string legacyPath = ReadString("Advanced", "LogFile", "", iniPath);
  g_config.logPath =
      ReadString("Diagnostics", "LogPath", legacyPath.c_str(), iniPath);

  // [Diagnostics] Enabled — the frame inspector is a dev tool; default OFF (the
  // shipping ini omits this key). Add [Diagnostics] Enabled=1 to turn it on.
  g_config.diagnostics =
      GetPrivateProfileIntA("Diagnostics", "Enabled", 0, iniPath.c_str()) != 0;

  // [Diagnostics] CaptureKey=0x7A   (virtual-key code; base auto-detected)
  std::string keyStr = ReadString("Diagnostics", "CaptureKey", "0x7A", iniPath);
  unsigned long vk = strtoul(keyStr.c_str(), nullptr, 0);
  if (vk > 0 && vk <= 0xFF) g_config.captureKey = static_cast<unsigned>(vk);

  // [Diagnostics] ProbeRVA=0x1188100  (matrix-dump probe; 0 disables)
  std::string prStr = ReadString("Diagnostics", "ProbeRVA", "0x1188100", iniPath);
  g_config.probeRVA = static_cast<unsigned>(strtoul(prStr.c_str(), nullptr, 0));

  // [Scaling] Enabled — legacy render-side scaling, superseded by UiScale;
  // default OFF (the shipping ini omits this key).
  g_config.renderSideScale =
      GetPrivateProfileIntA("Scaling", "Enabled", 0, iniPath.c_str()) != 0;

  // [Scaling] RemapInput=1   (mouse coordinate remap)
  g_config.remapInput =
      GetPrivateProfileIntA("Scaling", "RemapInput", 1, iniPath.c_str()) != 0;

  // [Scaling] ToggleKey=0x79   (runtime on/off hotkey)
  std::string tkStr = ReadString("Scaling", "ToggleKey", "0x79", iniPath);
  unsigned long tvk = strtoul(tkStr.c_str(), nullptr, 0);
  if (tvk > 0 && tvk <= 0xFF) g_config.toggleKey = static_cast<unsigned>(tvk);

  // [Scaling] UiScale=1.0   (the real fix: scale the GUI2 reference canvas)
  std::string usStr = ReadString("Scaling", "UiScale", "1.0", iniPath);
  float us = static_cast<float>(atof(usStr.c_str()));
  if (us >= 1.0f && us <= 4.0f) g_config.uiScale = us;

  // [Display] Borderless=0   (borderless-windowed mode)
  g_config.borderless =
      GetPrivateProfileIntA("Display", "Borderless", 0, iniPath.c_str()) != 0;

  // [SourcePatch] Enabled=0   (experimental GUI2 +0xF0 default-scale patch)
  g_config.sourcePatch =
      GetPrivateProfileIntA("SourcePatch", "Enabled", 0, iniPath.c_str()) != 0;

  // [Diagnostics] DiscoverSignatures=0   (porting aid; read-only, default off)
  g_config.discoverSignatures =
      GetPrivateProfileIntA("Diagnostics", "DiscoverSignatures", 0,
                            iniPath.c_str()) != 0;

  // [Signatures] — manual lever overrides for porting to other editions. Every
  // key defaults to "unset"; a stock install has no such section and is
  // unaffected. Blank string => 0 (RVA) / unset (signature); see config.h.
  auto readRva = [&](const char* key) -> unsigned {
    std::string s = ReadString("Signatures", key, "", iniPath);
    return s.empty() ? 0u
                     : static_cast<unsigned>(strtoul(s.c_str(), nullptr, 0));
  };
  auto readOff = [&](const char* key) -> int {
    std::string s = ReadString("Signatures", key, "", iniPath);
    return s.empty() ? -1 : static_cast<int>(strtol(s.c_str(), nullptr, 0));
  };
  g_config.ovrCreateMainRVA = readRva("CreateMainRVA");
  g_config.ovrCreateAltRVA = readRva("CreateAltRVA");
  g_config.ovrDesktopGlobalRVA = readRva("DesktopGlobalRVA");
  g_config.ovrCanvasRectOff = readRva("CanvasRectOffset");
  g_config.sigAltGuard = ReadString("Signatures", "AltGuardSig", "", iniPath);
  g_config.sigAltGuardGlobalOff = readOff("AltGuardGlobalOff");
  g_config.sigCreatorStore = ReadString("Signatures", "CreatorStoreSig", "", iniPath);
  g_config.sigCreatorStoreGlobalOff = readOff("CreatorStoreGlobalOff");
  g_config.sigPrologue = ReadString("Signatures", "PrologueSig", "", iniPath);
}

const Config& GetConfig() { return g_config; }
