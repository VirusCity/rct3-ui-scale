// config.* — user-facing settings read from an .ini at startup.
//
// No behaviour is hardcoded: the scale factor and logging toggle live in
// d3d9_uiscale.ini next to the DLL. Missing keys fall back to the defaults
// below so the mod still runs without an .ini present.
#pragma once

#include <string>

struct Config {
  // UI scale multiplier. 1.0 == stock size. >1.0 enlarges the UI.
  float scale = 1.5f;

  // Master logging switch. Keep on while iterating, off for normal play.
  bool logEnabled = true;

  // Absolute log-file path. Empty -> default (d3d9_uiscale.log next to the DLL).
  std::string logPath;
};

// Parse `iniPath` into the process-wide config. Call once at DLL attach.
void LoadConfig(const std::string& iniPath);

// Access the loaded config. Valid after LoadConfig(); returns defaults before.
const Config& GetConfig();
