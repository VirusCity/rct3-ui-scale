# RCT3 UI Scale

A Direct3D 9 proxy/hook DLL that makes **RollerCoaster Tycoon 3's UI bigger** so it
stays usable at 1440p, 4K, and beyond. The stock game lays its UI out at a fixed
pixel size with no scaling option, so toolbars, windows and fonts shrink as
resolution rises. This mod fixes that — and as a bonus adds a borderless-windowed
display mode.

Targets **RCT3: Complete Edition, Steam (App ID 1368820)**, 32-bit (x86). See
[CLAUDE.md](CLAUDE.md) for the full design notes and reverse-engineering rules.

> ⚠️ **Build-specific.** Every memory address and byte signature in this mod was
> reverse-engineered from this exact Steam build. It is **not** expected to work on
> the Platinum/GOG editions or other releases without re-deriving those signatures.

## Features
- **UI scaling** (`UiScale`) — enlarges the entire UI: toolbars, windows, and fonts.
  Rather than scaling pixels after the fact, it shrinks the game's internal UI
  reference canvas so the game *itself* lays the UI out larger. That keeps
  edge-anchoring correct **and** keeps mouse hit-testing aligned — clicks land
  exactly where the larger buttons are drawn.
- **Borderless-windowed mode** (`Borderless`) — strips the window border and sizes
  the window to cover the monitor, rendering at desktop resolution. RCT3 itself only
  offers windowed or exclusive fullscreen.
- **Transparent D3D9 proxy** — forwards every real `d3d9.dll` export, coexists with
  the Steam overlay, and renders identically to stock when scaling is set to 1.0.
- **In-process frame inspector** — a textual substitute for RenderDoc (which dropped
  D3D9 support); used during development to analyse the UI render pass.

## How the scaling works
RCT3's GUI2 system lays the entire UI out on an internal reference canvas, then maps
that canvas onto the display. The mod locates that canvas by byte-signature scan (no
hardcoded addresses) and shrinks it by `UiScale`. Because the game draws and
hit-tests against the same canvas, both visuals and clicks scale together. See
[research/ui_pass_findings.md](research/ui_pass_findings.md) for the frame analysis
that led here.

### Why not a render-side hook? (explored and abandoned)
The obvious approach — hook the D3D9 draw calls and scale the UI quads in flight —
**was tried first and abandoned.** The frame inspector showed the UI is drawn with
fixed-function, pre-transformed (`XYZRHW`) vertices, so they *can* be rewritten to
look bigger, but two problems make it unusable: (1) it does **not** fix the game's
hit-testing — clicks still land on the original tiny buttons; and (2) a uniform
vertex scale can't honour per-element edge anchoring, since the hook only sees final
pixels. The source patch above sidesteps both because the game itself lays the UI out
larger. The abandoned render-side code is kept in `src/ui_scale.*` for reference.

## Install (for players)
1. Download `d3d9.dll` and `d3d9_uiscale.ini` (build them, or grab a release).
2. Copy both next to the game executable — the folder containing `RCT3.exe`.
3. Edit `d3d9_uiscale.ini`: set `UiScale` (e.g. `1.25`) and optionally `Borderless=1`.
4. Launch the game.

To uninstall, delete `d3d9.dll` (and the `.ini`/`.log`) from the game folder.

If launch crashes or shows a black screen, suspect Steam-overlay interaction — set
`LoggingEnabled=1` and check the `d3d9_uiscale.log` that appears next to the DLL.

## Build (Win32 / x86 only)
Requires Visual Studio 2026 with the Desktop C++ workload (MSVC x86 + Windows SDK +
CMake). From the repo root:

```sh
cmake --preset vs2026-x86
cmake --build --preset vs2026-x86-release
```

Output: `build/Release/d3d9.dll` (32-bit), also copied to the repo root for
convenience. The build **fails configuration** if it isn't targeting x86 — a 64-bit
DLL cannot load into the 32-bit game.

## Configuration
All keys live in [`config/d3d9_uiscale.ini`](config/d3d9_uiscale.ini); each is
optional and documented inline. Key ones:

| Key | Default | Meaning |
|-----|---------|---------|
| `[Scaling] UiScale`     | `1.25` | UI size multiplier. `1.0` = stock. ~`1.15`–`1.5` is comfortable at 1440p/4K. |
| `[Display] Borderless`  | `0`    | `1` = borderless-windowed at desktop resolution. |
| `[Diagnostics] LoggingEnabled` | `1` | Write a timestamped `d3d9_uiscale.log` next to the DLL. |
| `[Diagnostics] DiscoverSignatures` | `0` | Porting aid: log a ready-to-paste `[Signatures]` block for non-Steam editions (see below). |

## Layout
| Path | Purpose |
|------|---------|
| `src/dllmain.cpp`     | Entry point, one-time wiring |
| `src/d3d9_hooks.*` / `src/d3d9_proxy_exports.*` | Proxy/export forwarding + MinHook device hooks |
| `src/source_patch.*`  | The UI-scale patch (shrinks the GUI2 reference canvas) |
| `src/sigscan.*`       | Byte-signature scanner that resolves the patch targets |
| `src/borderless.*`    | Borderless-windowed display mode |
| `src/ui_scale.*`      | Render-side scaling experiment (superseded by the source patch) |
| `src/frame_inspect.*` | In-process frame capture (RenderDoc substitute) |
| `src/input_remap.*`   | Mouse-coordinate remapping helpers |
| `src/config.*`        | `.ini` reader |
| `src/logging.*`       | Timestamped log |
| `src/d3d9.def`        | Export table (names + ordinals from the real DLL) |
| `config/`             | User-editable `.ini` |
| `research/`           | Frame-inspector findings (Ghidra notes stay local/gitignored) |
| `third_party/minhook` | Vendored MinHook (x86), under its own zlib license |
| `tools/ghidra`        | Helper scripts used during reverse engineering |

## Compatibility & caveats
- Only verified against **RCT3: Complete Edition, Steam build (App ID 1368820)**,
  32-bit. Other editions may need their signatures re-derived. Before reaching for
  a debugger, try `[Diagnostics] DiscoverSignatures=1`: the mod scans the running
  game, validates the UI canvas against your display, and logs a ready-to-paste
  `[Signatures]` block. Paste it into the `.ini` (`[Signatures]`), set
  `DiscoverSignatures=0`, and restart. A debugger is only needed if discovery
  reports that the structural patterns don't fit the build at all.
- At high `UiScale` values some edge-docked toolbars can extend past the screen edge;
  lower the value if that happens.
- The Steam in-game overlay also hooks D3D9. The proxy forwards exports cleanly to
  coexist, but report any overlay-related crashes with a log.

## License
[MIT](LICENSE). Vendored MinHook (`third_party/minhook`) is distributed under its own
zlib license — see `third_party/minhook/LICENSE.txt`.

This is an unofficial fan-made modification. RollerCoaster Tycoon 3 is a trademark of
its respective owners; this project is not affiliated with or endorsed by them, and
ships no game code or assets.
