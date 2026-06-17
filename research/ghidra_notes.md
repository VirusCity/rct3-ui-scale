# Ghidra / x64dbg notes — RCT3 (Steam, App ID 1368820)

> Build-specific addresses, offsets, signatures, and struct layouts you recover
> from **this exact executable**. Per CLAUDE.md, Claude will NEVER invent these —
> anything used in code must appear here first, captured by you.
>
> Do not paste copyrighted disassembly listings; record addresses, signatures,
> and your own descriptions. This file is gitignored.

## Goal
Find where the game computes UI element pixel coordinates/sizes — the code that
fills the XYZRHW UI vertex buffer, and the layout the mouse hit-test also reads —
so we can scale at the source. That fixes BOTH the visuals and clicking, unlike a
render-side scale. (See research/ui_pass_findings.md for why.)

## What we already know (from the frame inspector)
- UI draws: `DrawIndexedPrimitive`, fixed-function (no vertex shader),
  `FVF=0x144` (`XYZRHW|DIFFUSE|TEX1`) and `0x44` (`XYZRHW|DIFFUSE|TEX0`).
- Vertex stride for 0x144 = **28 bytes (0x1C)**: position x,y,z,rhw (4 floats,
  16B) + DIFFUSE color (4B) + one UV pair (8B).
- The UI lays out in fixed pixels (it shrinks as resolution rises), so there may
  be **no** resolution scaling to flip — we may need to *inject* a multiply.

## Step 1 — get the exact call-site (now automated by the mod)
Capture a UI frame (press F11 in-game). At the end of `d3d9_uiscale.log`, the mod
prints lines like:
`  UI draw call-site: <module>+0x<RVA>  (abs 0x..., module base 0x...)`
Record them here — these are the precise addresses to open in Ghidra (RVA is
relative to that module's image base).

- UI draw call-site(s): `____`
- Which module: `____`  (expected: the main game exe)

## Step 2 — from the call-site, find the quad/vertex builder
At that RVA in Ghidra you're at the code that issues the UI draw. Walk UP the
call tree: who writes the vertex buffer it draws from? Look for stores of
`(x, y, 0.0, 1.0, color, u, v)` at stride 0x1C, or a `Lock`/`memcpy` filling it.
Identify the routine that turns a widget rect `(x, y, w, h)` into 4 corner verts.

- Quad/vertex builder address: `____`
- How widget (x,y,w,h) reaches it (params? struct? register?): `____`

## Step 3 — find the scale or base resolution
Near the builder / layout code, look for:
- reads of the live screen size (3840 / 2160 / 2142) feeding position math,
- a base/virtual-resolution constant (common: 640, 480, 800, 600, 1024, 768),
- a global float == 1.0 multiplied into coordinates (a latent "UI scale").

- Screen-size reads: `____`
- Base-resolution constant(s): `____`
- Candidate UI-scale global (addr/type/value): `____`
- Verdict: is there an existing scale to change, or must we inject one? `____`

## Step 4 — confirm hit-testing shares the coordinates
Find where the cursor position is compared against widget rects (the click/hover
hit-test). Confirm it reads the SAME rect values we'd scale; if so, scaling the
layout fixes clicks for free. Tip: in x64dbg, set a hardware breakpoint on a
widget-rect address and hover/click to catch both the draw reader and the
hit-test reader.

- Mouse hit-test routine: `____`
- Does it share the layout coords with the builder? `yes / no` — `____`

## Other signatures
| Purpose | Address / RVA / AOB | Module | Notes |
|---------|---------------------|--------|-------|
|         |                     |        |       |

---
Once Steps 1–4 have addresses, Claude writes the in-memory patch from our DLL
(VirtualProtect + write, or a code hook) to scale the UI about the correct anchor.
