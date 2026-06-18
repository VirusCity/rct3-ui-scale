// ui_scale.* — the scaling logic, isolated from the proxy/export plumbing
// (d3d9_hooks.*). The hook detours call into these notification points; all
// decisions about WHAT to scale and HOW live here.
//
// Status: MILESTONE 1 — no geometry is altered. These entry points currently
// only count frames / log device lifecycle so we can confirm the hook chain is
// live and the game renders identically to stock. The actual UI-pass detection
// and scale transform are gated on the user's RenderDoc findings
// (research/renderdoc_findings.md) — see ui_scale.cpp for the marked TODO.
#pragma once

#include <d3d9.h>

namespace uiscale {

// Called once after the device is created and its methods are hooked.
void OnDeviceCreated(IDirect3DDevice9* device);

// Called every frame. Present is the canonical end-of-frame signal.
void OnPresent(IDirect3DDevice9* device);

// Called at EndScene (after the scene's draw calls have been submitted).
void OnEndScene(IDirect3DDevice9* device);

// Device-loss handling: the device vtable survives a Reset, but any state or
// resources we create for scaling must be released before / recreated after.
void OnPreReset(IDirect3DDevice9* device);
void OnPostReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp);

// Pick the 3x3-zone anchor for a point in the viewport. Shared by the vertex
// scaling (anchor = element bbox center) and the input remap (anchor = cursor),
// so the mouse inverse-transform matches the visual transform exactly.
void PickZoneAnchor(float px, float py, float left, float top, float W, float H,
                    float& ax, float& ay);

// Render-side proof: called from the VB-based draw detours BEFORE the real draw.
// If this looks like a UI draw (fixed-function + XYZRHW) and the bound vertex
// buffer is readable, scales the used vertices' x/y about screen center by the
// configured factor, in place. No-op for world draws or unreadable buffers.
// `firstVertex`/`vertexCount` describe the vertices the draw will consume.
void ScaleDrawIfUI(IDirect3DDevice9* device, UINT firstVertex, UINT vertexCount);

}  // namespace uiscale
