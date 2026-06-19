// borderless.* — optional borderless-windowed ("borderless fullscreen") mode.
//
// RCT3 ships only windowed or exclusive-fullscreen. This turns either into a
// borderless window that covers the monitor: we force the D3D9 device windowed
// with a backbuffer the size of the desktop, then strip the window chrome and
// position it over the monitor. Pure D3D9 + Win32 at the proxy boundary — no
// game internals involved. Gated behind [Display] Borderless (default off).
#pragma once

#include <windows.h>

struct _D3DPRESENT_PARAMETERS_;  // <d3d9.h> type, forward-declared

namespace borderless {

// Call BEFORE the real CreateDevice / Reset: if borderless is enabled, force the
// present params to windowed with a desktop-sized backbuffer. `wnd` selects the
// target monitor (may be null on Reset — the last known device window is used).
void PrepareParams(_D3DPRESENT_PARAMETERS_* pp, HWND wnd);

// Call AFTER a successful CreateDevice / Reset: strip the window border and
// position it to cover the monitor. `wnd` may be null (uses the last known).
void ApplyWindow(HWND wnd);

// Cheap per-Present guard: if the game put the title bar / a non-covering size
// back, re-assert the borderless window. No-op when disabled or unchanged.
void Tick();

}  // namespace borderless
