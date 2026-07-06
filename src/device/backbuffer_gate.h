// device/backbuffer_gate.h — startup-trap defense.
//
// During D3D device negotiation the engine may momentarily report placeholder
// resolutions (640×480, 1024×768) before the real backbuffer is bound
// (CLAUDE.md "device-negotiation startup trap"). Nothing downstream — scale
// resolution, discovery, patching — may run until this gate confirms a stable
// backbuffer:
//   * the ACTUAL backbuffer descriptor (GetBackBuffer→GetDesc, not the
//     presentation parameters the engine requested),
//   * unchanged across N presented frames,
//   * with a stricter N when the dims equal a known negotiation placeholder
//     (a real 1024×768 mode is legal — it just has to prove itself longer).
//
// Hooks used: IDirect3D9::CreateDevice, IDirect3DDevice9::Reset & ::Present —
// lifecycle observers only. Draw calls are never hooked (Hard Rules).
#pragma once

#include <d3d9.h>

namespace gate {

// Fired once per device generation when the backbuffer is confirmed stable.
using StableCallback = void (*)(IDirect3DDevice9* dev, unsigned bbWidth,
                                unsigned bbHeight);
// Fired every Present while stable (drives the discovery state machine).
using TickCallback = void (*)(IDirect3DDevice9* dev);
// Fired when the device Resets (resolution change / alt-tab): all applied
// state must be restored and re-validated against the NEW backbuffer.
using ResetCallback = void (*)();

void SetCallbacks(StableCallback onStable, TickCallback onTick,
                  ResetCallback onReset);

// Called by the proxy when the game obtains an IDirect3D9.
void OnDirect3DCreated(IDirect3D9* d3d);

bool IsStable();
unsigned Width();
unsigned Height();

}  // namespace gate
