// window/borderless.h — optional borderless windowed mode.
//
// Two jobs, both gated on [Borderless] Enabled:
//  * PrepareParams: rewrite the D3D present parameters to windowed at the
//    monitor's native resolution (or a configured size), so the game can
//    render ABOVE its 1080p in-game cap. Called before CreateDevice/Reset.
//  * ApplyWindow / Tick: strip the window chrome and cover the monitor, and
//    re-assert if the game puts the border back.
//
// This is complementary to UI scaling: a larger backbuffer feeds the same
// canvas scale (e.g. a 4K desktop -> auto 2x UI). It touches only the window
// and present params — no draw hooks.
#pragma once

#include <windows.h>

#include <d3d9.h>

namespace borderless {

// Rewrites pp for windowed + target resolution. Safe no-op when disabled.
void PrepareParams(D3DPRESENT_PARAMETERS* pp, HWND wnd);

// Strips chrome and covers the monitor. Call after the device is created/reset.
void ApplyWindow(HWND wnd);

// Cheap re-assert if the game restored the border or moved the window.
void Tick();

}  // namespace borderless
