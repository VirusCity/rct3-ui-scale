// ui_scale.* — the scaling logic, isolated from the proxy/export plumbing
// (d3d9_hooks.*). The hook detours call into these notification points; all
// decisions about WHAT to scale and HOW live here.
//
// Status: EXPLORED AND ABANDONED. This render-side path scales the UI by
// rewriting the XYZRHW vertices of the UI draws in flight. It is kept for
// reference but is NOT the shipped fix: per the frame analysis in
// research/ui_pass_findings.md it cannot fix the game's hit-testing (clicks
// still land on the original tiny buttons) and a uniform vertex scale can't
// honour per-element edge anchoring. The shipped fix is the source patch in
// source_patch.* (shrinks the GUI2 reference canvas so the game lays the UI
// out larger itself). See ui_scale.cpp for details.
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

// Arm a one-shot diagnostic dump of the next `n` scaled UI draws' bbox+anchor.
void RequestRectDump(int n);

// Map a window-message cursor (x,y) back to the original layout coordinate IF it
// is over a scaled UI element (using that element's exact anchor/scale). Returns
// false when the cursor is not over any UI element, so the caller passes world /
// 3D cursors through unchanged. Thread-safe.
bool MapCursorToOriginal(int x, int y, int& ox, int& oy);

// Render-side proof: called from the VB-based draw detours BEFORE the real draw.
// If this looks like a UI draw (fixed-function + XYZRHW) and the bound vertex
// buffer is readable, scales the used vertices' x/y about screen center by the
// configured factor, in place. No-op for world draws or unreadable buffers.
// `firstVertex`/`vertexCount` describe the vertices the draw will consume.
void ScaleDrawIfUI(IDirect3DDevice9* device, UINT firstVertex, UINT vertexCount);

}  // namespace uiscale
