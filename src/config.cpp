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

  // [General] LoggingEnabled=1
  g_config.logEnabled =
      GetPrivateProfileIntA("General", "LoggingEnabled", 1, iniPath.c_str()) != 0;

  // [Advanced] LogFile=  (optional override; blank => default path)
  g_config.logPath = ReadString("Advanced", "LogFile", "", iniPath);

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

  // [SourcePatch] Enabled=0   (experimental GUI2 +0xF0 default-scale patch)
  g_config.sourcePatch =
      GetPrivateProfileIntA("SourcePatch", "Enabled", 0, iniPath.c_str()) != 0;
}

const Config& GetConfig() { return g_config; }
