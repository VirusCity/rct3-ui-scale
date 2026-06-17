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

  // [Diagnostics] Enabled=1
  g_config.diagnostics =
      GetPrivateProfileIntA("Diagnostics", "Enabled", 1, iniPath.c_str()) != 0;

  // [Diagnostics] CaptureKey=0x7A   (virtual-key code; base auto-detected)
  std::string keyStr = ReadString("Diagnostics", "CaptureKey", "0x7A", iniPath);
  unsigned long vk = strtoul(keyStr.c_str(), nullptr, 0);
  if (vk > 0 && vk <= 0xFF) g_config.captureKey = static_cast<unsigned>(vk);
}

const Config& GetConfig() { return g_config; }
