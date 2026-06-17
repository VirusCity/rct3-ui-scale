# RenderDoc findings — RCT3 UI render pass

> Your own captures from **this** Steam build (App ID 1368820). Claude reads this
> file and writes scaling code from it. Nothing here is assumed — if a field is
> blank, the corresponding code is not written yet.

## How to capture
1. Launch RCT3 through RenderDoc (or inject), get into a scenario with UI visible
   (toolbars, a window open). Capture a frame (F12 / PrtScn).
2. Open the capture, walk the draw calls in the Event Browser from top to bottom.
3. The 3D world is drawn first (perspective, depth testing). The UI is typically
   drawn last, as 2D quads. Find where the world ends and the UI begins.

## What to record

### Resolution captured at
- Backbuffer size: `____ x ____`
- Windowed / fullscreen: `____`

### The world→UI boundary
- Approx. event ID (EID) where UI drawing starts: `____`
- What changes at that boundary (the signal we'll key on):

| Render state            | World draws | UI draws |
|-------------------------|-------------|----------|
| ZENABLE (depth test)    |             |          |
| ZWRITEENABLE            |             |          |
| ALPHABLENDENABLE        |             |          |
| CULLMODE                |             |          |
| Vertex shader (FF/VS?)  |             |          |
| Pixel shader            |             |          |

### UI draw call shape
- Primitive type (e.g. TRIANGLELIST): `____`
- FVF / vertex declaration (does it include RHW / pre-transformed XYZRHW?): `____`
- Are positions already in **screen pixels** (XYZRHW) or transformed by a matrix?

### Transforms (if the UI uses the fixed-function matrices)
- WORLD: `____`
- VIEW: `____`
- PROJECTION (ortho? what extents?): `____`
- Viewport (X, Y, Width, Height): `____`

### A representative UI draw call (paste vertex positions)
```
; e.g. first 4 verts of a toolbar quad: x, y, z, rhw, ...
```

### Notes / surprises
-
