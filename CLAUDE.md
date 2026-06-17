# CLAUDE.md — RCT3 UI Scaling Mod

## Project goal
Build a Direct3D 9 hook DLL that rescales RollerCoaster Tycoon 3's user interface so it
stays usable at 1440p, 4K, and other high resolutions. The stock game renders the UI at a
fixed pixel size with no scaling option, so it shrinks as resolution rises. This mod
intercepts the game's D3D9 UI render pass and scales it up.

## Target environment (do NOT assume any other build)
- **Game:** RollerCoaster Tycoon 3: Complete Edition, Steam version (App ID **1368820**).
- **Architecture:** 32-bit (x86) Windows app using Direct3D 9. ALL code, the output DLL,
  and every dependency MUST be built for **Win32 / x86** — never x64.
- **OS:** Windows 10/11.
- Any memory address, function offset, byte signature, or struct layout is **specific to
  this exact Steam build**. It is NOT portable from the Platinum edition, the GOG release,
  or any tutorial found online. All such data must come from the user's own reverse
  engineering of this build (see Workflow). **Never hardcode an address you did not get
  directly from the user.**

## Approach
**Primary strategy — `d3d9.dll` proxy + runtime hook:**
- The DLL is named `d3d9.dll` and placed next to the game executable so Windows' DLL
  search order loads it before the system copy.
- It must forward (proxy) all real `d3d9.dll` exports to the genuine system DLL so the
  game still launches and renders normally.
- It hooks the D3D9 device with MinHook (vtable hooks on `IDirect3DDevice9`) to intercept
  the draw calls that render the 2D UI, then applies a scale transform to those quads
  about their screen anchor points.
- Detecting the UI pass: the UI is typically drawn with depth testing disabled, alpha
  blending enabled, and an orthographic/identity transform, unlike the perspective-
  projected 3D world. CONFIRM the exact heuristic against the user's frame-inspector
  dumps (see Workflow) before relying on it; do not assume.

**Secondary strategy — investigate in parallel:** if Ghidra/x64dbg reveals a single
UI-scale factor or the UI coordinate-computation routine, a small in-memory patch may be
cleaner than the full render hook. Discuss tradeoffs when the user shares findings.

## Hard rules
- **Never read, write, or reference files inside the game install directory.** Work only
  inside this project folder. The user copies the built DLL into the game folder manually.
- **Never commit game assets** — no `.ovl` files, extracted sprites, the game executable,
  or disassembly of copyrighted code. `.gitignore` must exclude them. `research/` holds
  only the user's own notes.
- **Never fabricate addresses, offsets, or signatures.** If build-specific data is needed
  and not yet provided, say so and tell the user exactly what to capture.
- The **Steam in-game overlay also hooks Direct3D 9.** The proxy must coexist with it
  (clean export forwarding, defensive init). Flag overlay-related causes if the user
  reports crashes or a black screen on launch.

## Build
- **Toolchain:** CMake + MSVC (Visual Studio 2026), configured for **Win32 (x86)**.
  (CMake generator string: `Visual Studio 18 2026`; see `CMakePresets.json`.)
- **Hooking library:** MinHook, x86 build, vendored under `third_party/`.
- **Language:** C++17.
- The build produces `d3d9.dll`. The user deploys it — do NOT attempt to copy it into the
  game folder.

## Code conventions
- **Logging is mandatory and central.** This mod is debugged blind, so `logging.*` should
  write a timestamped log file capturing hook init, device creation, and per-frame UI-pass
  detection counts. Keep it cheap and toggleable so it can stay on during testing and off
  for normal play.
- **No hardcoded scale.** User-facing config (scale factor, logging on/off) lives in an
  `.ini` read at startup via `config.*`.
- Keep the proxy/export layer (`d3d9_hooks.*`) separate from the scaling logic
  (`ui_scale.*`) so scaling can be iterated without touching export plumbing.

## Known hard problems (raise these proactively)
1. **Separating UI draws from world draws.** Too-broad detection scales the 3D scene;
   too-narrow misses UI elements. Refine the heuristic against the frame-inspector dumps.
2. **Input / hit-testing.** Scaling only the visuals means clicks still register at the
   original (tiny) positions. Mouse coordinates must be remapped to the scaled UI, or the
   scaling done in a way the game's own hit-testing respects. Treat this as a first-class
   requirement, not an afterthought.
3. **Anchoring.** Elements anchored to screen edges/corners must scale about the correct
   anchor, not the origin, or they drift off-screen.

## Workflow
The user drives all live analysis; Claude writes and refines code from what they report.
- **RenderDoc is NOT usable** — it dropped Direct3D 9 support, and DXVK-bridging fails
  (the 32-bit process runs out of address space for a capture). Instead, frame analysis
  is done **in-process** by the mod's own **frame inspector** (`frame_inspect.*`):
  pressing the capture hotkey (INI `[Diagnostics] CaptureKey`, default F11) dumps the
  next frame's draw calls — primitive type, render state, FVF/vertex-shader usage, and
  transforms — to the log. The user pastes the relevant capture into
  `research/ui_pass_findings.md`. This is our textual substitute for a RenderDoc capture.
- User uses **Ghidra / x64dbg** for static/dynamic analysis and records addresses and
  signatures in `research/ghidra_notes.md`.
- Claude reads those notes, proposes changes as reviewable diffs, and explains what to test.
- User builds in Visual Studio (or Claude builds locally to verify), copies the DLL into
  the game, runs, and pastes the log output back. Iterate.

## Directory map
- `src/` — all mod source (`dllmain`, `d3d9_hooks`, `ui_scale`, `config`, `logging`).
- `third_party/` — MinHook (vendored).
- `config/` — the user-editable `.ini`.
- `research/` — user's reverse-engineering notes (game-derived notes stay gitignored).
- `build/` — gitignored build output.
