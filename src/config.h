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

  // Diagnostic probe: on capture, dump 16 floats (a 4x4 matrix) read from
  // exe_base + probeRVA. Used to characterise the GUI2 global transform matrix
  // (research/ghidra_notes.md: static VA 0x1588100 -> RVA 0x1188100). 0 = off.
  // Sourced from headless analysis; override here if the build differs.
  unsigned probeRVA = 0x1188100;

  // --- Render-side scaling proof -------------------------------------------
  // When on, UI draws (fixed-function XYZRHW) have their vertex x/y scaled about
  // screen center by `scale`, in place, just before the draw. This is the
  // visual proof-of-concept; it does NOT yet remap mouse input (clicks still
  // land at original positions). Turn off to return to the transparent proxy.
  bool renderSideScale = true;

  // Remap the mouse coordinates the game reads (window messages) by the inverse
  // UI transform, so clicks land on the scaled elements. Separate toggle so it
  // can be A/B tested against world-picking behaviour.
  bool remapInput = true;

  // Virtual-key that toggles scaling on/off at runtime — e.g. turn it off on the
  // main menu / scenario select, on once in a park. Default 0x79 = F10.
  unsigned toggleKey = 0x79;

  // --- Source patch (experimental, see research/ghidra_notes.md) -----------
  // When on, at startup we signature-patch the GUI2 element constructors so the
  // per-element scale at `element + 0xF0` defaults to `scale` instead of 1.0,
  // i.e. ask the game to lay the UI out bigger itself (fixing render AND clicks
  // at the source). Independent of renderSideScale — do NOT enable both at once
  // or the UI is scaled twice. Default off until the experiment is confirmed.
  bool sourcePatch = false;
};

// Parse `iniPath` into the process-wide config. Call once at DLL attach.
void LoadConfig(const std::string& iniPath);

// Access the loaded config. Valid after LoadConfig(); returns defaults before.
const Config& GetConfig();
