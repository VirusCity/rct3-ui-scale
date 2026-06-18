// input_remap.* — make clicks land on the scaled UI.
//
// We only scale the *rendered* UI vertices, so the game still hit-tests at the
// original (small) positions. Ghidra showed RCT3's GetCursorPos is used only for
// 3D-picking / resolution handling, NOT the UI cursor — so the UI cursor comes
// from window messages. We subclass the game window and inverse-transform the
// mouse coordinates in WM_MOUSE* messages (the same 3x3-zone transform the
// renderer applies), so the game's UI hit-test sees the position that matches
// where the scaled element now appears. World picking (GetCursorPos) is left
// untouched.
//
// This is the input half of the render-side scaling experiment; toggled by
// [Scaling] RemapInput.
#pragma once

#include <windows.h>

namespace inputremap {

// Subclass `hwnd`'s window procedure to remap UI mouse coordinates. Idempotent;
// call once the device window is known (device creation). Must run on the thread
// that owns the window (the game's main thread).
void Install(HWND hwnd);

// Restore the original window procedure. Call at DLL detach.
void Remove();

}  // namespace inputremap
