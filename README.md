# RCT3 UI Scale

A Direct3D 9 proxy/hook DLL that rescales RollerCoaster Tycoon 3's UI so it stays
usable at 1440p/4K. Targets **RCT3: Complete Edition, Steam (App ID 1368820)**,
32-bit. See [CLAUDE.md](CLAUDE.md) for the full design and rules.

## Status
**Milestone 1 — transparent proxy + live hooks + frame inspector (no scaling yet).**
The DLL loads, forwards every real `d3d9.dll` export, hooks the device
(Reset/Present/EndScene + the Draw* calls), and logs device creation + frames. It
renders **identically to stock** — that's the point: verify the hook chain before
touching geometry. Verified live at 3840×2160 fullscreen.

To analyse the UI render pass (RenderDoc can't be used — no D3D9 support), press
the capture hotkey (default **F11**) in-game; the next frame's draw calls dump to
the log. The actual UI scaling is gated on that capture (see `research/`).

## Build (Win32 / x86 only)
Requires Visual Studio 2026 with the Desktop C++ workload (MSVC x86 + Windows SDK
+ CMake). From the repo root:

```sh
cmake --preset vs2026-x86
cmake --build --preset vs2026-x86-release
```

Output: `build/Release/d3d9.dll` (32-bit). Or open the folder in Visual Studio —
the preset forces the Win32 target.

## Deploy (done manually by you — Claude never touches the game folder)
Copy next to the game executable (the folder with `RCT3.exe`):
- `d3d9.dll`  (the build output)
- `d3d9_uiscale.ini`  (from `config/`, edit `ScaleFactor` to taste)

Launch the game. With `LoggingEnabled=1` a `d3d9_uiscale.log` appears next to the
DLL — paste it back when reporting results.

## Layout
| Path | Purpose |
|------|---------|
| `src/dllmain.cpp`     | Entry point, one-time wiring |
| `src/d3d9_hooks.*`    | Proxy/export forwarding + MinHook device hooks |
| `src/ui_scale.*`      | Scaling logic (isolated from the plumbing) |
| `src/frame_inspect.*` | In-process frame capture (RenderDoc substitute) |
| `src/config.*`        | `.ini` reader |
| `src/logging.*`       | Timestamped log |
| `src/d3d9.def`        | Export table (names + ordinals from the real DLL) |
| `config/`             | User-editable `.ini` |
| `research/`           | Your frame-inspector / Ghidra notes |
| `third_party/minhook` | Vendored MinHook (x86) |

## Notes
- The Steam in-game overlay also hooks D3D9; the proxy forwards exports cleanly to
  coexist. If launch crashes or shows a black screen, suspect overlay interaction
  and check the log.
- 32-bit is mandatory — the build fails configuration if pointers aren't 4 bytes.
