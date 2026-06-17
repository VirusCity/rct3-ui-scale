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

## Summary (filled from the 2026-06-17 capture, frame 4683, 3840×2142)

- Total draws in the frame: **311** (draw 0–310).
- UI pass: the solid UI block is **draws ~252–310** (all `XYZRHW`, `VS=0`). The 3D
  world is **draws 0–248**. Draw 249 is a lone `VS=0` `XYZRHW` TRISTRIP (likely a
  full-screen overlay/fade); draws 250–251 briefly return to `VS=1` (probably a
  3D element inside a UI window). The inspector's old "draw 0" marker was a false
  positive (a world draw with Z off) — heuristic fixed to `VS==0 && XYZRHW`.
- **Do UI draws use `XYZRHW` (pre-transformed screen-space) verts?**  **YES.**
  FVF `0x144` (`XYZRHW|DIFFUSE|TEX1`) dominates, with some `0x44`
  (`XYZRHW|DIFFUSE|TEX0`). Positions are baked screen pixels in a vertex buffer.
  → world/view/projection AND viewport scaling are all bypassed for these draws.
- UI draws are **fixed-function (`VS=0`)**. The entire 3D world is programmable
  (`VS=1`). This `VS=0 && XYZRHW` pair is the clean UI discriminator.
- Render state on UI draws — Z: `ZENABLE=1` but `ZWRITE=0` (2D, depth no-op);
  AlphaBlend: **on**; Cull: **NONE (1)**; Lighting: 1 (irrelevant for XYZRHW).
- World→UI boundary: cleanest signal is **`VS` 1→0 together with `XYZRHW`
  appearing**. `ZENABLE` alone is NOT reliable (world draw 0 had Z off).

## Strategy implications

- **The easy render-side scale is off the table.** `XYZRHW` verts ignore
  SetTransform and are not rescaled by the viewport — so we can't enlarge the UI
  by composing a matrix or shrinking the viewport.
- **Render-side option (visual only):** intercept the UI draws (identifiable by
  `VS=0 && XYZRHW`) and scale their vertex x/y about an anchor by routing through
  our own scaled vertex buffer. Two hard caveats: (a) it does NOT fix hit-testing
  — the game still places buttons at the original tiny pixels, so clicks miss;
  (b) a uniform scale can't honour per-element edge anchoring, since the hook only
  sees final pixels. Good for a visual proof, not a usable UI.
- **Source-patch option (Ghidra, likely the real fix):** find where the game
  computes the UI pixel coordinates (the code that fills the XYZRHW vertex buffer
  from a base/virtual canvas), and scale there. Because the game itself lays the
  UI out bigger, BOTH rendering and the game's own hit-testing stay consistent.
  This is CLAUDE.md's "secondary strategy" and now looks like the primary one.
  Next RE step: see research/ghidra_notes.md.

## Pasted capture
[13:02:57.303] === FRAME CAPTURE START (frame 4683) ===
[13:02:57.304]   viewport X=0 Y=0 W=3840 H=2142 minZ=0.00 maxZ=1.00
[13:02:57.305]   legend: Z=ZENABLE Zw=ZWRITE A=ALPHABLEND cull=CULLMODE L=LIGHTING VS=vertexshader
[13:02:57.312]   >> likely UI pass begins at draw 0 <<
[13:02:57.313]      PROJECTION row0 [1.000 0.000 0.000 0.000] row3 [0.000 0.000 0.000 1.000]
[13:02:57.315]   [draw   0] DrawPrimitive          TRILIST  prims=2     | Z=0 Zw=0 A=0 cull=1 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.316]   -- STATE CHANGE @ draw 1: Z 0->1  A 0->1  VS 1->1 --
[13:02:57.317]   [draw   1] DrawPrimitive          TRILIST  prims=74    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.319]   -- STATE CHANGE @ draw 2: Z 1->0  A 1->0  VS 1->1 --
[13:02:57.320]   [draw   2] DrawIndexedPrimitive   TRILIST  prims=40    | Z=0 Zw=0 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.321]   -- STATE CHANGE @ draw 3: Z 0->0  A 0->1  VS 1->1 --
[13:02:57.323]   [draw   3] DrawIndexedPrimitive   TRILIST  prims=40    | Z=0 Zw=0 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.324]   [draw   4] DrawIndexedPrimitive   TRILIST  prims=135   | Z=0 Zw=0 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.325]   -- STATE CHANGE @ draw 5: Z 0->1  A 1->0  VS 1->0 --
[13:02:57.326]   [draw   5] DrawPrimitive          TRISTRIP prims=2     | Z=1 Zw=0 A=0 cull=1 L=1 VS=0 FVF=0x44 (XYZRHW DIFFUSE TEX0)
[13:02:57.328]   -- STATE CHANGE @ draw 6: Z 1->0  A 0->1  VS 0->1 --
[13:02:57.329]   [draw   6] DrawIndexedPrimitive   TRILIST  prims=27    | Z=0 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.329]   [draw   7] DrawIndexedPrimitive   TRILIST  prims=27    | Z=0 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.331]   -- STATE CHANGE @ draw 8: Z 0->1  A 1->1  VS 1->1 --
[13:02:57.333]   [draw   8] DrawIndexedPrimitive   TRILIST  prims=712   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.333]   [draw   9] DrawIndexedPrimitive   TRILIST  prims=1812  | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.334]   -- STATE CHANGE @ draw 10: Z 1->1  A 1->0  VS 1->1 --
[13:02:57.335]   [draw  10] DrawPrimitive          TRILIST  prims=1     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.337]   [draw  11] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.339]   [draw  12] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.340]   [draw  13] DrawIndexedPrimitive   TRILIST  prims=1664  | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.341]   [draw  14] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.343]   -- STATE CHANGE @ draw 15: Z 1->1  A 0->1  VS 1->1 --
[13:02:57.344]   [draw  15] DrawIndexedPrimitive   TRILIST  prims=1122  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.345]   [draw  16] DrawIndexedPrimitive   TRILIST  prims=250   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.346]   [draw  17] DrawIndexedPrimitive   TRILIST  prims=1156  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.348]   [draw  18] DrawIndexedPrimitive   TRILIST  prims=146   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.349]   [draw  19] DrawIndexedPrimitive   TRILIST  prims=42    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.350]   [draw  20] DrawIndexedPrimitive   TRILIST  prims=776   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.350]   [draw  21] DrawIndexedPrimitive   TRILIST  prims=126   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.352]   [draw  22] DrawIndexedPrimitive   TRILIST  prims=652   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.353]   [draw  23] DrawIndexedPrimitive   TRILIST  prims=86    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.355]   [draw  24] DrawIndexedPrimitive   TRILIST  prims=1334  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.355]   [draw  25] DrawIndexedPrimitive   TRILIST  prims=58    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.357]   [draw  26] DrawIndexedPrimitive   TRILIST  prims=1234  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.358]   [draw  27] DrawIndexedPrimitive   TRILIST  prims=1464  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.359]   [draw  28] DrawIndexedPrimitive   TRILIST  prims=240   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.360]   [draw  29] DrawIndexedPrimitive   TRILIST  prims=1180  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.362]   [draw  30] DrawIndexedPrimitive   TRILIST  prims=310   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.363]   [draw  31] DrawIndexedPrimitive   TRILIST  prims=62    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.364]   [draw  32] DrawIndexedPrimitive   TRILIST  prims=624   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.364]   [draw  33] DrawIndexedPrimitive   TRILIST  prims=306   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.367]   [draw  34] DrawIndexedPrimitive   TRILIST  prims=1960  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.368]   [draw  35] DrawIndexedPrimitive   TRILIST  prims=982   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.369]   [draw  36] DrawIndexedPrimitive   TRILIST  prims=1584  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.370]   [draw  37] DrawIndexedPrimitive   TRILIST  prims=1240  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.372]   [draw  38] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.373]   [draw  39] DrawIndexedPrimitive   TRILIST  prims=644   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.374]   [draw  40] DrawIndexedPrimitive   TRILIST  prims=414   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x52 (XYZ NORMAL DIFFUSE TEX0)
[13:02:57.375]   [draw  41] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.377]   [draw  42] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.378]   [draw  43] DrawIndexedPrimitive   TRILIST  prims=1664  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.379]   [draw  44] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x12 (XYZ NORMAL TEX0)
[13:02:57.380]   -- STATE CHANGE @ draw 45: Z 1->1  A 1->0  VS 1->1 --
[13:02:57.382]   [draw  45] DrawIndexedPrimitive   TRILIST  prims=24    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.383]   -- STATE CHANGE @ draw 46: Z 1->1  A 0->1  VS 1->1 --
[13:02:57.385]   [draw  46] DrawIndexedPrimitive   TRILIST  prims=118   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.386]   [draw  47] DrawIndexedPrimitive   TRILIST  prims=102   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.387]   [draw  48] DrawIndexedPrimitive   TRILIST  prims=34    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.388]   [draw  49] DrawIndexedPrimitive   TRILIST  prims=46    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.389]   [draw  50] DrawIndexedPrimitive   TRILIST  prims=4     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.390]   [draw  51] DrawIndexedPrimitive   TRILIST  prims=4     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.392]   [draw  52] DrawIndexedPrimitive   TRILIST  prims=40    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.393]   [draw  53] DrawIndexedPrimitive   TRILIST  prims=64    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.394]   [draw  54] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.395]   [draw  55] DrawIndexedPrimitive   TRILIST  prims=36    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.397]   [draw  56] DrawIndexedPrimitive   TRILIST  prims=114   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.398]   [draw  57] DrawIndexedPrimitive   TRILIST  prims=38    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.399]   [draw  58] DrawIndexedPrimitive   TRILIST  prims=12    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.400]   [draw  59] DrawIndexedPrimitive   TRILIST  prims=12    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.403]   [draw  60] DrawIndexedPrimitive   TRILIST  prims=10    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.403]   [draw  61] DrawIndexedPrimitive   TRILIST  prims=90    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.404]   [draw  62] DrawIndexedPrimitive   TRILIST  prims=6     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.406]   [draw  63] DrawIndexedPrimitive   TRILIST  prims=6     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.407]   [draw  64] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.409]   [draw  65] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.410]   -- STATE CHANGE @ draw 66: Z 1->1  A 1->0  VS 1->1 --
[13:02:57.411]   [draw  66] DrawIndexedPrimitive   TRILIST  prims=576   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.413]   [draw  67] DrawIndexedPrimitive   TRILIST  prims=2396  | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.414]   [draw  68] DrawIndexedPrimitive   TRILIST  prims=132   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.415]   [draw  69] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.416]   [draw  70] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.418]   [draw  71] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.419]   [draw  72] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.419]   [draw  73] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.420]   [draw  74] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.422]   [draw  75] DrawIndexedPrimitive   TRILIST  prims=90    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.423]   [draw  76] DrawIndexedPrimitive   TRILIST  prims=90    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.424]   [draw  77] DrawIndexedPrimitive   TRILIST  prims=178   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.425]   [draw  78] DrawIndexedPrimitive   TRILIST  prims=178   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.427]   -- STATE CHANGE @ draw 79: Z 1->1  A 0->1  VS 1->1 --
[13:02:57.428]   [draw  79] DrawIndexedPrimitive   TRILIST  prims=320   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.429]   [draw  80] DrawIndexedPrimitive   TRILIST  prims=928   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.430]   [draw  81] DrawPrimitive          TRILIST  prims=200   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.432]   [draw  82] DrawPrimitive          TRILIST  prims=200   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.433]   [draw  83] DrawPrimitive          TRILIST  prims=62    | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.434]   [draw  84] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.436]   [draw  85] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.437]   [draw  86] DrawIndexedPrimitive   TRILIST  prims=1664  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.438]   [draw  87] DrawIndexedPrimitive   TRILIST  prims=2048  | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.439]   -- STATE CHANGE @ draw 88: Z 1->0  A 1->1  VS 1->0 --
[13:02:57.440]   [draw  88] DrawIndexedPrimitive   TRILIST  prims=2     | Z=0 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x44 (XYZRHW DIFFUSE TEX0)
[13:02:57.443]   -- STATE CHANGE @ draw 89: Z 0->1  A 1->0  VS 0->1 --
[13:02:57.444]   [draw  89] DrawIndexedPrimitive   TRILIST  prims=448   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.444]   [draw  90] DrawIndexedPrimitive   TRILIST  prims=167   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.446]   [draw  91] DrawIndexedPrimitive   TRILIST  prims=166   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.447]   [draw  92] DrawIndexedPrimitive   TRILIST  prims=306   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.448]   [draw  93] DrawIndexedPrimitive   TRILIST  prims=196   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.449]   [draw  94] DrawIndexedPrimitive   TRILIST  prims=192   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.450]   [draw  95] DrawIndexedPrimitive   TRILIST  prims=332   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.452]   [draw  96] DrawIndexedPrimitive   TRILIST  prims=482   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.453]   [draw  97] DrawIndexedPrimitive   TRILIST  prims=108   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.454]   [draw  98] DrawIndexedPrimitive   TRILIST  prims=312   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.456]   [draw  99] DrawIndexedPrimitive   TRILIST  prims=104   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.457]   [draw 100] DrawIndexedPrimitive   TRILIST  prims=284   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.458]   [draw 101] DrawIndexedPrimitive   TRILIST  prims=268   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.459]   [draw 102] DrawIndexedPrimitive   TRILIST  prims=1080  | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.460]   [draw 103] DrawIndexedPrimitive   TRILIST  prims=504   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.462]   [draw 104] DrawIndexedPrimitive   TRILIST  prims=30    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.463]   [draw 105] DrawIndexedPrimitive   TRILIST  prims=512   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.464]   [draw 106] DrawIndexedPrimitive   TRILIST  prims=387   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.464]   [draw 107] DrawIndexedPrimitive   TRILIST  prims=388   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.467]   [draw 108] DrawIndexedPrimitive   TRILIST  prims=428   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.468]   [draw 109] DrawIndexedPrimitive   TRILIST  prims=388   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.469]   [draw 110] DrawIndexedPrimitive   TRILIST  prims=1014  | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.470]   [draw 111] DrawIndexedPrimitive   TRILIST  prims=788   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.472]   [draw 112] DrawIndexedPrimitive   TRILIST  prims=594   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.473]   [draw 113] DrawIndexedPrimitive   TRILIST  prims=244   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.474]   [draw 114] DrawIndexedPrimitive   TRILIST  prims=399   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.475]   [draw 115] DrawIndexedPrimitive   TRILIST  prims=782   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.477]   [draw 116] DrawIndexedPrimitive   TRILIST  prims=14    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.477]   [draw 117] DrawIndexedPrimitive   TRILIST  prims=79    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.479]   [draw 118] DrawIndexedPrimitive   TRILIST  prims=12    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.480]   [draw 119] DrawIndexedPrimitive   TRILIST  prims=174   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.482]   [draw 120] DrawIndexedPrimitive   TRILIST  prims=303   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.483]   [draw 121] DrawIndexedPrimitive   TRILIST  prims=223   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.484]   [draw 122] DrawIndexedPrimitive   TRILIST  prims=330   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.485]   [draw 123] DrawIndexedPrimitive   TRILIST  prims=123   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.487]   [draw 124] DrawIndexedPrimitive   TRILIST  prims=110   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.488]   [draw 125] DrawIndexedPrimitive   TRILIST  prims=112   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.489]   [draw 126] DrawIndexedPrimitive   TRILIST  prims=143   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.491]   [draw 127] DrawIndexedPrimitive   TRILIST  prims=15    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.492]   [draw 128] DrawIndexedPrimitive   TRILIST  prims=116   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.493]   [draw 129] DrawIndexedPrimitive   TRILIST  prims=246   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.494]   [draw 130] DrawIndexedPrimitive   TRILIST  prims=252   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.496]   [draw 131] DrawIndexedPrimitive   TRILIST  prims=103   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.497]   [draw 132] DrawIndexedPrimitive   TRILIST  prims=231   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.498]   [draw 133] DrawIndexedPrimitive   TRILIST  prims=160   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.499]   [draw 134] DrawIndexedPrimitive   TRILIST  prims=198   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.500]   [draw 135] DrawIndexedPrimitive   TRILIST  prims=132   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.502]   [draw 136] DrawIndexedPrimitive   TRILIST  prims=30    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.503]   [draw 137] DrawIndexedPrimitive   TRILIST  prims=142   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.504]   [draw 138] DrawIndexedPrimitive   TRILIST  prims=162   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.505]   [draw 139] DrawIndexedPrimitive   TRILIST  prims=416   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.507]   [draw 140] DrawIndexedPrimitive   TRILIST  prims=273   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.507]   [draw 141] DrawIndexedPrimitive   TRILIST  prims=85    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.510]   [draw 142] DrawIndexedPrimitive   TRILIST  prims=169   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.511]   [draw 143] DrawIndexedPrimitive   TRILIST  prims=124   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.512]   [draw 144] DrawIndexedPrimitive   TRILIST  prims=140   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.513]   [draw 145] DrawIndexedPrimitive   TRILIST  prims=227   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.514]   [draw 146] DrawIndexedPrimitive   TRILIST  prims=131   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.515]   [draw 147] DrawIndexedPrimitive   TRILIST  prims=57    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.517]   [draw 148] DrawIndexedPrimitive   TRILIST  prims=106   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.519]   [draw 149] DrawIndexedPrimitive   TRILIST  prims=32    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.519]   [draw 150] DrawIndexedPrimitive   TRILIST  prims=52    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.521]   [draw 151] DrawIndexedPrimitive   TRILIST  prims=22    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.522]   [draw 152] DrawIndexedPrimitive   TRILIST  prims=13    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.523]   [draw 153] DrawIndexedPrimitive   TRILIST  prims=45    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.524]   [draw 154] DrawIndexedPrimitive   TRILIST  prims=26    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.525]   [draw 155] DrawIndexedPrimitive   TRILIST  prims=35    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.527]   [draw 156] DrawIndexedPrimitive   TRILIST  prims=35    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.528]   [draw 157] DrawIndexedPrimitive   TRILIST  prims=132   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x112 (XYZ NORMAL TEX1)
[13:02:57.529]   [draw 158] DrawIndexedPrimitive   TRILIST  prims=16    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.530]   [draw 159] DrawIndexedPrimitive   TRILIST  prims=150   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.532]   [draw 160] DrawIndexedPrimitive   TRILIST  prims=345   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0 (decl/shader)
[13:02:57.533]   [draw 161] DrawIndexedPrimitive   TRILIST  prims=645   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0 (decl/shader)
[13:02:57.534]   [draw 162] DrawIndexedPrimitive   TRILIST  prims=780   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0 (decl/shader)
[13:02:57.535]   [draw 163] DrawIndexedPrimitive   TRILIST  prims=225   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0 (decl/shader)
[13:02:57.537]   [draw 164] DrawIndexedPrimitive   TRILIST  prims=20    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.538]   [draw 165] DrawIndexedPrimitive   TRILIST  prims=82    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.539]   [draw 166] DrawIndexedPrimitive   TRILIST  prims=32    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.541]   [draw 167] DrawIndexedPrimitive   TRILIST  prims=32    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.543]   [draw 168] DrawIndexedPrimitive   TRILIST  prims=204   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.544]   [draw 169] DrawIndexedPrimitive   TRILIST  prims=102   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.545]   [draw 170] DrawIndexedPrimitive   TRILIST  prims=102   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.546]   [draw 171] DrawIndexedPrimitive   TRILIST  prims=102   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.547]   [draw 172] DrawIndexedPrimitive   TRILIST  prims=102   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.548]   [draw 173] DrawIndexedPrimitive   TRILIST  prims=48    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.550]   [draw 174] DrawIndexedPrimitive   TRILIST  prims=99    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.551]   [draw 175] DrawIndexedPrimitive   TRILIST  prims=99    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.553]   [draw 176] DrawIndexedPrimitive   TRILIST  prims=150   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.554]   [draw 177] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.555]   [draw 178] DrawIndexedPrimitive   TRILIST  prims=72    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.555]   [draw 179] DrawIndexedPrimitive   TRILIST  prims=68    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.557]   [draw 180] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.558]   -- STATE CHANGE @ draw 181: Z 1->1  A 0->1  VS 1->1 --
[13:02:57.559]   [draw 181] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.560]   -- STATE CHANGE @ draw 182: Z 1->1  A 1->0  VS 1->1 --
[13:02:57.562]   [draw 182] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.563]   -- STATE CHANGE @ draw 183: Z 1->1  A 0->1  VS 1->1 --
[13:02:57.564]   [draw 183] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.564]   -- STATE CHANGE @ draw 184: Z 1->1  A 1->0  VS 1->1 --
[13:02:57.567]   [draw 184] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.568]   -- STATE CHANGE @ draw 185: Z 1->1  A 0->1  VS 1->1 --
[13:02:57.569]   [draw 185] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.570]   -- STATE CHANGE @ draw 186: Z 1->1  A 1->0  VS 1->1 --
[13:02:57.572]   [draw 186] DrawIndexedPrimitive   TRILIST  prims=16    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.574]   [draw 187] DrawIndexedPrimitive   TRILIST  prims=54    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.575]   [draw 188] DrawIndexedPrimitive   TRILIST  prims=236   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0 (decl/shader)
[13:02:57.575]   [draw 189] DrawIndexedPrimitive   TRILIST  prims=704   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.577]   [draw 190] DrawIndexedPrimitive   TRILIST  prims=88    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.578]   [draw 191] DrawIndexedPrimitive   TRILIST  prims=128   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.579]   [draw 192] DrawIndexedPrimitive   TRILIST  prims=112   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.580]   [draw 193] DrawIndexedPrimitive   TRILIST  prims=48    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.582]   [draw 194] DrawIndexedPrimitive   TRILIST  prims=560   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.583]   [draw 195] DrawIndexedPrimitive   TRILIST  prims=42    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.584]   [draw 196] DrawIndexedPrimitive   TRILIST  prims=294   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.585]   [draw 197] DrawIndexedPrimitive   TRILIST  prims=26    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.587]   [draw 198] DrawIndexedPrimitive   TRILIST  prims=182   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.588]   [draw 199] DrawIndexedPrimitive   TRILIST  prims=16    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.589]   [draw 200] DrawIndexedPrimitive   TRILIST  prims=223   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.590]   [draw 201] DrawIndexedPrimitive   TRILIST  prims=64    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.592]   [draw 202] DrawIndexedPrimitive   TRILIST  prims=11    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.593]   [draw 203] DrawIndexedPrimitive   TRILIST  prims=40    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.593]   [draw 204] DrawIndexedPrimitive   TRILIST  prims=27    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.594]   [draw 205] DrawIndexedPrimitive   TRILIST  prims=208   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.596]   [draw 206] DrawIndexedPrimitive   TRILIST  prims=616   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.597]   [draw 207] DrawIndexedPrimitive   TRILIST  prims=10    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.598]   [draw 208] DrawIndexedPrimitive   TRILIST  prims=144   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.599]   [draw 209] DrawIndexedPrimitive   TRILIST  prims=22    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.601]   [draw 210] DrawIndexedPrimitive   TRILIST  prims=58    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.602]   [draw 211] DrawIndexedPrimitive   TRILIST  prims=6     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.603]   [draw 212] DrawIndexedPrimitive   TRILIST  prims=218   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.604]   [draw 213] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.606]   [draw 214] DrawIndexedPrimitive   TRILIST  prims=288   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.608]   [draw 215] DrawIndexedPrimitive   TRILIST  prims=24    | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.609]   [draw 216] DrawIndexedPrimitive   TRILIST  prims=36    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.610]   [draw 217] DrawIndexedPrimitive   TRILIST  prims=360   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.612]   [draw 218] DrawIndexedPrimitive   TRILIST  prims=480   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.613]   [draw 219] DrawIndexedPrimitive   TRILIST  prims=120   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0 (decl/shader)
[13:02:57.614]   [draw 220] DrawIndexedPrimitive   TRILIST  prims=480   | Z=1 Zw=1 A=0 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.615]   [draw 221] DrawIndexedPrimitive   TRILIST  prims=512   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.617]   [draw 222] DrawIndexedPrimitive   TRILIST  prims=20    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.618]   [draw 223] DrawIndexedPrimitive   TRILIST  prims=556   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.619]   [draw 224] DrawIndexedPrimitive   TRILIST  prims=237   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.620]   [draw 225] DrawIndexedPrimitive   TRILIST  prims=237   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.622]   [draw 226] DrawIndexedPrimitive   TRILIST  prims=272   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.624]   [draw 227] DrawIndexedPrimitive   TRILIST  prims=252   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.625]   [draw 228] DrawIndexedPrimitive   TRILIST  prims=720   | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.626]   [draw 229] DrawIndexedPrimitive   TRILIST  prims=4     | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.628]   [draw 230] DrawIndexedPrimitive   TRILIST  prims=12    | Z=1 Zw=1 A=0 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.629]   -- STATE CHANGE @ draw 231: Z 1->1  A 0->1  VS 1->1 --
[13:02:57.630]   [draw 231] DrawIndexedPrimitive   TRILIST  prims=48    | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.631]   [draw 232] DrawIndexedPrimitive   TRILIST  prims=486   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.632]   [draw 233] DrawIndexedPrimitive   TRILIST  prims=56    | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.634]   [draw 234] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.635]   [draw 235] DrawIndexedPrimitive   TRILIST  prims=24    | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.636]   [draw 236] DrawIndexedPrimitive   TRILIST  prims=40    | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.637]   [draw 237] DrawIndexedPrimitive   TRILIST  prims=4     | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.638]   [draw 238] DrawIndexedPrimitive   TRILIST  prims=18    | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.639]   [draw 239] DrawIndexedPrimitive   TRILIST  prims=398   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.640]   [draw 240] DrawIndexedPrimitive   TRILIST  prims=398   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.643]   [draw 241] DrawIndexedPrimitive   TRILIST  prims=138   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.644]   [draw 242] DrawIndexedPrimitive   TRILIST  prims=324   | Z=1 Zw=1 A=1 cull=3 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.645]   [draw 243] DrawIndexedPrimitive   TRILIST  prims=48    | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.646]   [draw 244] DrawIndexedPrimitive   TRILIST  prims=48    | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.647]   [draw 245] DrawIndexedPrimitive   TRILIST  prims=6     | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.648]   [draw 246] DrawIndexedPrimitive   TRILIST  prims=6     | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.649]   [draw 247] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.651]   [draw 248] DrawIndexedPrimitive   TRILIST  prims=40    | Z=1 Zw=1 A=1 cull=1 L=1 VS=1 FVF=0x152 (XYZ NORMAL DIFFUSE TEX1)
[13:02:57.652]   -- STATE CHANGE @ draw 249: Z 1->0  A 1->1  VS 1->0 --
[13:02:57.653]   [draw 249] DrawPrimitive          TRISTRIP prims=2     | Z=0 Zw=1 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.655]   -- STATE CHANGE @ draw 250: Z 0->1  A 1->1  VS 0->1 --
[13:02:57.655]   [draw 250] DrawIndexedPrimitive   TRILIST  prims=186   | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.657]   [draw 251] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=3 L=1 VS=1 FVF=0x142 (XYZ DIFFUSE TEX1)
[13:02:57.658]   -- STATE CHANGE @ draw 252: Z 1->1  A 1->1  VS 1->0 --
[13:02:57.659]   [draw 252] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.661]   [draw 253] DrawIndexedPrimitive   TRILIST  prims=18    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.662]   [draw 254] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.663]   [draw 255] DrawIndexedPrimitive   TRILIST  prims=20    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.664]   [draw 256] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.665]   [draw 257] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.667]   [draw 258] DrawIndexedPrimitive   TRILIST  prims=24    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.668]   [draw 259] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.669]   [draw 260] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.670]   [draw 261] DrawIndexedPrimitive   TRILIST  prims=12    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.671]   [draw 262] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.673]   [draw 263] DrawIndexedPrimitive   TRILIST  prims=38    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.674]   [draw 264] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.675]   [draw 265] DrawIndexedPrimitive   TRILIST  prims=136   | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.677]   [draw 266] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.677]   [draw 267] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.678]   [draw 268] DrawIndexedPrimitive   TRILIST  prims=76    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.679]   [draw 269] DrawIndexedPrimitive   TRILIST  prims=26    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.681]   [draw 270] DrawIndexedPrimitive   TRILIST  prims=18    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.682]   [draw 271] DrawIndexedPrimitive   TRILIST  prims=452   | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.683]   [draw 272] DrawIndexedPrimitive   TRILIST  prims=26    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.684]   [draw 273] DrawIndexedPrimitive   TRILIST  prims=38    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.686]   [draw 274] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.687]   [draw 275] DrawIndexedPrimitive   TRILIST  prims=36    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.688]   [draw 276] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.690]   [draw 277] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.692]   [draw 278] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.693]   [draw 279] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.694]   [draw 280] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.695]   [draw 281] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.696]   [draw 282] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.697]   [draw 283] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.699]   [draw 284] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.700]   [draw 285] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.702]   [draw 286] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.703]   [draw 287] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.704]   [draw 288] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.705]   [draw 289] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.707]   [draw 290] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.708]   [draw 291] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.709]   [draw 292] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.709]   [draw 293] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.711]   [draw 294] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.713]   [draw 295] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.713]   [draw 296] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.715]   [draw 297] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.716]   [draw 298] DrawIndexedPrimitive   TRILIST  prims=28    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.717]   [draw 299] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.718]   [draw 300] DrawIndexedPrimitive   TRILIST  prims=8     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.719]   [draw 301] DrawIndexedPrimitive   TRILIST  prims=52    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.721]   [draw 302] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.722]   [draw 303] DrawIndexedPrimitive   TRILIST  prims=4     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x44 (XYZRHW DIFFUSE TEX0)
[13:02:57.723]   [draw 304] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.724]   [draw 305] DrawIndexedPrimitive   TRILIST  prims=16    | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.726]   [draw 306] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.727]   [draw 307] DrawIndexedPrimitive   TRILIST  prims=100   | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.728]   [draw 308] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.729]   [draw 309] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.731]   [draw 310] DrawIndexedPrimitive   TRILIST  prims=2     | Z=1 Zw=0 A=1 cull=1 L=1 VS=0 FVF=0x144 (XYZRHW DIFFUSE TEX1)
[13:02:57.733] === FRAME CAPTURE END: 311 draws; UI boundary @ draw 0 ===

## Notes / surprises
-
