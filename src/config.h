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

  // --- Diagnostics: the built-in frame inspector ---------------------------
  // When enabled, pressing `captureKey` dumps the next frame's draw calls (with
  // render state / FVF / transforms) to the log — our in-process substitute for
  // RenderDoc, which doesn't support D3D9. Turn off for normal play.
  bool diagnostics = true;

  // Virtual-key code for the frame-capture hotkey. Default 0x7A = VK_F11
  // (F12 is avoided — it's the Steam screenshot key).
  unsigned captureKey = 0x7A;

  // --- Render-side scaling proof -------------------------------------------
  // When on, UI draws (fixed-function XYZRHW) have their vertex x/y scaled about
  // screen center by `scale`, in place, just before the draw. This is the
  // visual proof-of-concept; it does NOT yet remap mouse input (clicks still
  // land at original positions). Turn off to return to the transparent proxy.
  bool renderSideScale = true;
};

// Parse `iniPath` into the process-wide config. Call once at DLL attach.
void LoadConfig(const std::string& iniPath);

// Access the loaded config. Valid after LoadConfig(); returns defaults before.
const Config& GetConfig();
