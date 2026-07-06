# RCT3 UI Scale

Fixes the tiny UI in *RollerCoaster Tycoon 3* on modern high-resolution
displays. Drop two files next to the game and the interface renders at a
usable size — **2× at 4K, ~1.33× at 1440p, native at 1080p** — with mouse
input staying perfectly aligned. Optional borderless-windowed mode lets the
game render at your monitor's full resolution, above its built-in 1080p cap.

No per-version hacks: the mod discovers the game's UI canvas at runtime, so
one DLL works across editions. Confirmed scaling **on first launch** on:

| Edition | Executable |
| --- | --- |
| Complete Edition (Steam) | `RCT3.exe` |
| Platinum / Gold | `RCT3plus.exe` |
| Original 2004 demo | `RCT3.exe` |

Other builds (retail, GOG, community-patched, widescreen-fixed) are expected
to work the same way — the discovery is version-agnostic by design. If one
doesn't, see Troubleshooting.

## Install

1. Download the latest release zip.
2. Copy `d3d9.dll` and `d3d9_uiscale.ini` into the game folder, next to
   `RCT3.exe` (or `RCT3plus.exe`).
3. Play. The mod calibrates itself on the first launch — no setup.

To uninstall, delete the two files (plus `d3d9_uiscale.cache` if present).

## Configuration (`d3d9_uiscale.ini`)

| Setting | Default | Meaning |
| --- | --- | --- |
| `[Master] Enabled` | `1` | Master switch for the whole mod. |
| `[Features] Scale` | `0` | `0` = automatic (1× at 1080p → 2× at 4K). Any other value is an explicit uniform multiplier, e.g. `1.5`. |
| `[Features] Borderless` | `1` | Borderless window at your monitor's native resolution (lets the game render above its 1080p cap). |
| `[Debug] Cache / Logging / Verbose` | `1 / 0 / 0` | Diagnostics — leave alone unless advised. |

## How it works (short version)

The game lays its UI out against a canvas whose size the engine divides into
the screen resolution. The mod finds that canvas **at runtime, by data flow**
— no hardcoded addresses, no per-version signatures for data — hooks the
function that creates it, and shrinks it *before the UI lays out*, so the
engine's own math magnifies rendering and mouse hit-testing together. Every
patch is validated against the live backbuffer, fully reversible, and cached
per-executable (a game update just triggers a fresh self-calibration).

Details: [src/README.md](src/README.md).

## Troubleshooting

- **UI didn't scale?** Set `Logging=1` under `[Debug]`, relaunch, load into a
  park, quit, and open an issue with the `d3d9_uiscale.log` from the game
  folder. The log records exactly what was searched and why.
- **Weird state after a crash or game update?** Delete `d3d9_uiscale.cache`
  next to the exe — the mod recalibrates on the next launch. (It also detects
  both situations itself; this is just the manual override.)

## Building from source

Requirements: Visual Studio 2026 (or any MSVC toolchain CMake can drive) —
the game is 32-bit, so the Win32 preset is mandatory.

```
cmake --preset vs2026-x86
cmake --build build --config Release
```

The DLL lands in `build/Release/d3d9.dll`.

## License

MIT — see [LICENSE](LICENSE). MinHook (vendored under `third_party/`) is
BSD-2, see its bundled license.
