// source_patch.* — in-memory patches to the game's own UI layout, applied at
// startup. The "secondary strategy" from CLAUDE.md, now the PRIMARY fix.
//
// === The working UI-scale lever (see research/ghidra_notes.md) ===
// RCT3's GUI2 lays the entire UI out on a reference canvas equal to the device
// resolution, then maps it to screen pixels (scaleX = deviceW / canvasW). That
// canvas is the `RCTDesktop` rect at `desktop+0x7c..0x88` (L,T,R,B floats), set
// once at desktop creation by `FUN_007b42d0` to (0,0, deviceW, deviceH).
// Shrinking the canvas => larger scale => the whole UI is laid out bigger, and
// because the hit-test reads the SAME canvas, mouse clicks stay aligned. Doing
// it at creation (before any widget lays out) keeps edge-anchored toolbars on
// screen. Confirmed live in x32dbg (clicks perfect, anchoring correct).
//
// We hook `FUN_007b42d0`; right after it builds the desktop we divide the
// canvas right/bottom by the configured UiScale. Addresses below are specific to
// this Steam build (image base 0x400000, no ASLR) and come from our own RE.
//
// === Dead experiment (kept, gated off) ===
// `ApplyGui2ScaleDefault` patches the GUI2 element "+0xF0 = 1.0f" ctor writes.
// That offset turned out to be the AttractionView ride-zoom, not a UI scale, and
// is re-initialised to 1.0 at runtime — so it does nothing. Left for reference,
// gated behind [SourcePatch] Enabled (default off).
#pragma once

namespace sourcepatch {

// Install the UI-scale hook: detours the RCTDesktop creator and shrinks the GUI2
// reference canvas by `uiScale` (e.g. 1.25 => UI ~25% larger), so the game lays
// the whole UI out bigger with correct anchoring + hit-testing. No-op when
// uiScale <= 1.0. Requires MH_Initialize() to have run. Call once at attach,
// before the game creates its desktop. Returns true if the hook was installed.
bool InstallUiScaleHook(float uiScale);

// EXPERIMENT (dead, gated off): rewrite the GUI2 "+0xF0 = 1.0f" ctor defaults to
// `scale`. Returns the number of sites patched. See header comment.
int ApplyGui2ScaleDefault(float scale);

}  // namespace sourcepatch
