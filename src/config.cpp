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
}

const Config& GetConfig() { return g_config; }
