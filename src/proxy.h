// proxy.h — d3d9 proxy: the DLL's load vehicle.
//
// Windows loads our d3d9.dll (next to the game exe) instead of the system one.
// We forward every export to the real DLL; only Direct3DCreate9/Ex are wrapped
// so device/backbuffer_gate can observe device creation. No draw-call hooks —
// forbidden by the Hard Rules and not needed by this design.
#pragma once

namespace proxy {

// Loads the real system d3d9.dll and resolves all forwarded exports.
// Must succeed before the game calls any d3d9 export.
bool Init();

}  // namespace proxy
