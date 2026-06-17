# UI render-pass findings — RCT3 (Steam, App ID 1368820)

> Captured with the mod's built-in **frame inspector** (RenderDoc can't be used —
> no D3D9 support). Claude reads this file and writes scaling code from it.
> Nothing here is assumed — if a field is blank, the matching code isn't written.

## How to capture
1. Build & deploy `d3d9.dll` + `d3d9_uiscale.ini` next to the game exe. Ensure
   `[Diagnostics] Enabled=1` (default).
2. Launch RCT3, get into a scenario with UI visible (toolbars, a window open).
3. Press the capture key (`[Diagnostics] CaptureKey`, default **F11**). The next
   frame's draw calls are dumped to `d3d9_uiscale.log` between
   `=== FRAME CAPTURE START ===` and `=== FRAME CAPTURE END ===`.
4. Paste the capture below and fill in the summary.

## What the inspector logs (per draw call)
`[draw N] <Call> <PrimType> prims=K | Z=ZENABLE Zw=ZWRITE A=ALPHABLEND
cull=CULLMODE L=LIGHTING VS=<0|1> FVF=<decoded>` — plus `STATE CHANGE` markers
and a `>> likely UI pass begins at draw N <<` line (heuristic: depth test off or
a pre-transformed `XYZRHW` FVF appears).

## Summary to fill in (the questions that decide the strategy)

- Total draws in the frame: `____`
- Draw index where the UI pass starts (per the marker — and does it look right?): `____`
- **Do UI draws use `XYZRHW` (pre-transformed screen-space) verts?**  `yes / no`
  - If **yes**: positions are already in pixels in a vertex buffer → scaling means
    editing those coords (hard for DrawIndexedPrimitive; may push us toward the
    in-memory / coordinate-routine patch instead). The PROJECTION matrix is unused.
  - If **no** (UI uses a matrix): we can likely scale by composing a transform on
    the UI pass (SetTransform/SetViewport hook) about the right anchor.
- Are UI draws fixed-function (`VS=0`) or programmable (`VS=1`)? `____`
- Render state on UI draws — Z: `____`  AlphaBlend: `____`  Cull: `____`  Lighting: `____`
- Does the world→UI boundary show a clean state change we can key on? Which states?

## Pasted capture
```
(paste the FRAME CAPTURE block from d3d9_uiscale.log here)
```

## Notes / surprises
-
