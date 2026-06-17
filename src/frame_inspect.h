// frame_inspect.* — in-process frame capture, our substitute for RenderDoc
// (which doesn't support Direct3D 9).
//
// On a configurable hotkey, the next frame's draw calls are dumped to the log
// along with the render state, FVF / vertex-shader usage, and transforms that
// distinguish the 3D world pass from the 2D UI pass. This is the data the
// scaling work is gated on (see CLAUDE.md "Known hard problems" #1).
//
// It only reads device state and logs — no rendering is altered, and the cost
// is zero on frames that aren't being captured.
#pragma once

#include <d3d9.h>

namespace frameinspect {

// Called once per frame from the Present hook. Polls the capture hotkey and
// delimits a captured frame (start/finish). `device` is the live device.
void OnFrameBoundary(IDirect3DDevice9* device);

// Called from each Draw* detour. When a capture is active, logs the draw call
// and the device state at that point. `call` is the API name for the log;
// `caller` is the game's return address (from _ReturnAddress() in the detour),
// used to report the UI draw call-sites as module+RVA for static analysis.
void OnDraw(IDirect3DDevice9* device, const char* call, D3DPRIMITIVETYPE type,
            UINT primitiveCount, void* caller);

}  // namespace frameinspect
