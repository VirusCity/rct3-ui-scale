// d3d9_hooks.* — the proxy/export plumbing, kept separate from the scaling
// logic (ui_scale.*) so the two can be iterated independently (see CLAUDE.md).
//
// Responsibilities:
//   1. Proxy: load the *real* system d3d9.dll and resolve its exports so our
//      forwarding stubs (in the .cpp) and wrappers can call through.
//   2. Hooks: once the game creates an IDirect3D9 / IDirect3DDevice9, install
//      MinHook vtable hooks on the device methods we care about. The detours
//      delegate the actual scaling decisions to ui_scale.*.
//
// This file deliberately contains NO scaling math.
#pragma once

#include <d3d9.h>

namespace proxy {

// Load the genuine d3d9.dll from the system directory and resolve every export
// we forward or wrap. Call once at DLL attach, before any export can run.
// Returns false if the real DLL could not be loaded.
bool Init();

// Free the real DLL. Call at DLL detach.
void Shutdown();

}  // namespace proxy

namespace hooks {

// Install the CreateDevice hook on a freshly created IDirect3D9(Ex). Idempotent.
void InstallOnD3D9(IDirect3D9* d3d9);

// Install the device-method hooks (Reset/Present/EndScene, + draw calls later)
// on a freshly created device. Idempotent.
void InstallOnDevice(IDirect3DDevice9* device);

}  // namespace hooks
