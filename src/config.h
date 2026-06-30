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

  // --- Diagnostics: the built-in frame inspector (dev tool) ----------------
  // When enabled, pressing `captureKey` dumps the next frame's draw calls (with
  // render state / FVF / transforms) to the log — our in-process substitute for
  // RenderDoc, which doesn't support D3D9. Default OFF; enable via the .ini.
  bool diagnostics = false;

  // Virtual-key code for the frame-capture hotkey. Default 0x7A = VK_F11
  // (F12 is avoided — it's the Steam screenshot key).
  unsigned captureKey = 0x7A;

  // Diagnostic probe: on capture, dump 16 floats (a 4x4 matrix) read from
  // exe_base + probeRVA. Used to characterise the GUI2 global transform matrix
  // (research/ghidra_notes.md: static VA 0x1588100 -> RVA 0x1188100). 0 = off.
  // Sourced from headless analysis; override here if the build differs.
  unsigned probeRVA = 0x1188100;

  // --- Render-side scaling (legacy, superseded by uiScale) -----------------
  // Visual-only proof-of-concept: scales UI draw vertices in place. Superseded
  // by the uiScale source hook (which also fixes hit-testing). Default OFF.
  bool renderSideScale = false;

  // Remap the mouse coordinates the game reads (window messages) by the inverse
  // UI transform, so clicks land on the scaled elements. Separate toggle so it
  // can be A/B tested against world-picking behaviour.
  bool remapInput = true;

  // Virtual-key that toggles scaling on/off at runtime — e.g. turn it off on the
  // main menu / scenario select, on once in a park. Default 0x79 = F10.
  unsigned toggleKey = 0x79;

  // --- UI scale (the real fix: scale the GUI2 reference canvas) -------------
  // The game lays its whole UI out on a reference canvas = device resolution,
  // then scales it to pixels. We hook the RCTDesktop creator and shrink that
  // canvas by `uiScale`, so the game itself lays the UI out larger with correct
  // anchoring AND hit-testing (see research/ghidra_notes.md). 1.0 = off (stock).
  // 1.25 = 25% larger, 1.5 = 50%, etc. Clamped to 4.0.
  float uiScale = 1.0f;

  // --- Display: borderless-windowed mode -----------------------------------
  // When on, the D3D9 device is forced windowed at desktop resolution and the
  // game window is stripped of its border and sized to cover the monitor —
  // "borderless fullscreen". RCT3 itself only offers windowed or exclusive
  // fullscreen. Default off.
  bool borderless = false;

  // --- Source patch (DEAD experiment, see research/ghidra_notes.md) --------
  // Signature-patches the GUI2 element "+0xF0 = 1.0f" ctor writes. That offset
  // is the AttractionView ride-zoom, not a UI scale, and is reset at runtime —
  // so it does nothing. Kept for reference, default off.
  bool sourcePatch = false;

  // --- [Signatures]: manual lever overrides (ADVANCED; for porting) --------
  // For users on a non-Steam / non-Complete edition where the built-in
  // signatures don't resolve. ALL default to "unset" (0 / empty / -1), in which
  // case resolution falls through to the built-in signatures and then the baked
  // Steam fallback — i.e. the stock RCT3 Complete user is wholly unaffected.
  //
  // Per-value priority: INI RVA > INI signature > built-in signature > fallback.
  //
  // Option B — raw RVAs (exe-relative; image base subtracted). Most robust:
  unsigned ovrCreateMainRVA = 0;     // RCTDesktop creator (normal-play path)
  unsigned ovrCreateAltRVA = 0;      // identical twin creator
  unsigned ovrDesktopGlobalRVA = 0;  // s_desktop singleton pointer
  unsigned ovrCanvasRectOff = 0;     // canvas-rect struct offset (0 => default 0x7C)
  // Option A — AOB signatures (scanned; same syntax as sigscan.h). An empty
  // string keeps the built-in pattern; a negative offset keeps the built-in one.
  std::string sigAltGuard;
  int sigAltGuardGlobalOff = -1;     // disp32 offset of s_desktop within AltGuardSig
  std::string sigCreatorStore;
  int sigCreatorStoreGlobalOff = -1; // disp32 offset of the store target
  std::string sigPrologue;

  // [Diagnostics] DiscoverSignatures — read-only porting aid (default OFF). When
  // on, the mod scans for all lever-site candidates, validates the s_desktop
  // global + canvas offset against the live display resolution, and logs a
  // ready-to-paste [Signatures] block. No effect on scaling; pure reporting.
  bool discoverSignatures = false;
};

// Parse `iniPath` into the process-wide config. Call once at DLL attach.
void LoadConfig(const std::string& iniPath);

// Access the loaded config. Valid after LoadConfig(); returns defaults before.
const Config& GetConfig();
