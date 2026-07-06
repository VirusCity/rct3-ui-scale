# Architecture — RCT3 Universal UI Scaling (hybrid signature + data-flow)

A single proxy `d3d9.dll` that scales the *RollerCoaster Tycoon 3* UI to a
usable size at high resolutions/DPI, on any edition. No hardcoded RVAs, no
debugger, **no hardware watchpoints**: data-flow discovery finds WHERE the
canvas lives, signature hooks decide WHEN to act.

**Core principle: correct early hook + correct canvas discovery — or do
nothing.** There is no post-layout scaling fallback; the system never degrades
into approximate scaling.

## How it works

The game lays its UI out against a canvas root (`s_desktop`, an `RCTDesktop*`
in the exe's `.data`). The engine renders the UI scaled by `device ÷ canvas`,
so shrinking the canvas rect **before the UI lays out** magnifies everything —
rendering *and* hit-testing together (verified live on Complete + Gold).

Two independent subsystems cooperate:

1. **Passive data-flow discovery** (`discovery/passive_discovery.*`) — the
   source of truth for WHERE. Driven every frame by the Present hook, it scans
   the exe's writable data for pointer slots whose target object contains a
   float LTRB rect `[~0, ~0, bbW, bbH]`, i.e. it derives the **global slot
   RVA + rect offset** together. Candidates must match the live backbuffer
   exactly, at a stable object address, for N consecutive ticks (temporal
   coherence + structural stability). Only the RVA + offset are ever
   persisted — resolved pointers are never cached; every consumer re-resolves
   `canvas = *(slot); rect = canvas + offset` at each use.

2. **Signature timing hooks** (`hooks/timing_hook.*`) — WHEN. Wildcard AOB
   signatures (with INI-overridable fallbacks) locate the desktop *creator*
   functions and hook them with MinHook. Signatures are **timing-only**; they
   never locate data. Three tiers: INI overrides → built-in guard signatures
   (byte-verified on both known editions) → **store-derived**: given a
   *discovered* slot RVA, find the `mov [slot], reg` sites in code and walk
   back to the function prologue — data locating timing, which makes unknown
   editions work with zero per-version patterns.

Inside a creator detour (after the original runs, before the UI lays out):
resolve the mapping fresh → validate (pointer validity, rect sanity, exact
backbuffer match) → shrink right/bottom by the uniform scale → `Ready`.
Anything short of full validation does nothing.

### Disambiguation (why this can't pick a decoy)

Device-sized rects also live behind D3D wrapper globals (the old build-20
decoys). Acceptance therefore requires one of, in order of strength:

* **Creator write attribution**: the detour snapshots the exe's writable data
  before the original runs and diffs after — the slot *this* creator call
  wrote, resolving to a fresh backbuffer rect, is the canvas (the pure
  data-flow replacement for the old hardware write-watch).
* **Store-hook attribution**: a hook derived from a specific slot's own writer
  fired and that slot now resolves fresh.
* **Uniqueness**: exactly one candidate exists after a full scan pass.

Ambiguity is never guessed away: the stable candidate set is persisted as
**pending** cache entries, the next run arms store hooks on all of them before
the game creates anything, and the first creation confirms the real one.

### Self-healing

* Backbuffer change → `Discovering`; candidates re-prove against the new
  dimensions; the rebuilt canvas re-validates in the creator hook (or by N
  matching ticks). Cached mappings are re-verified before they could ever be
  discarded; a single-frame mismatch never fails anything. Timeout M without
  revalidation → `Failed` (do nothing) until new evidence arrives.
* **Crash cookie**: the cache carries an `Armed` flag while a session is armed
  to scale; it is cleared on the first Present after a successful shrink (and
  on clean detach). If it is still set at attach, the previous session died
  trusting that cache — it is discarded and rediscovered, breaking any crash
  loop automatically.

## Layout

| Path | Role |
| --- | --- |
| `dllmain.cpp` | Attach: config, log, MinHook, proxy, selector wiring. Patches **nothing**. |
| `proxy.*`, `proxy_exports.cpp`, `d3d9.def` | Load vehicle: forward every d3d9 export; wrap `Direct3DCreate9/Ex` only. |
| `window/borderless.*` | Optional borderless-windowed at monitor resolution (renders above the 1080p in-game cap; feeds the scale). |
| `device/backbuffer_gate.*` | Startup-trap defense + device lifecycle; publishes backbuffer dims to shared state at CreateDevice/Reset/Present; drives the persistent discovery tick. |
| `core/state.*` | Thread-safe shared state: `State`, packed mapping, backbuffer dims, creator serial. |
| `core/canvas_probe.*` | Fresh `*(slot)+off` resolution + validation (fault-proof; classifies Unscaled/Scaled/Dormant/Mismatch). |
| `core/scale.*` | Uniform scale `S = max(1, min(bb/Reference))` or the user override. |
| `discovery/passive_discovery.*` | Passive slot scan, temporal/structural filtering, creator write attribution, revalidation, pending/confirmed cache writes. |
| `sig/sigscan.*` | Wildcard AOB scanner over executable sections (ported from v1). |
| `hooks/timing_hook.*` | Creator-hook tiers + the validating, self-idempotent pre-layout shrink detour. |
| `apply/disable.*` | Safety net: restore all raw patches, patch nothing further. |
| `cache/strategy_cache.*` | PE-fingerprint-keyed cache: confirmed mapping, pending candidates, armed cookie. |
| `selector.*` | Wiring + lifecycle: cookie check, cache priming, hook arming, tick fan-out. |

## Run behavior

- **Known editions (Complete / Gold)**: guard signatures hook a creator at
  attach; **twin-store expansion** then hooks its byte-identical sibling(s)
  (a guard may land on a dead twin — Complete's alt path). The first canvas
  creation is therefore caught live and attributed (snapshot diff + xref
  dominance), so **run 1 scales pre-layout on both editions**.
- **Unknown editions**: run 1 discovers candidates passively and caches them;
  run 2 arms store-derived hooks from the cache and confirms + scales at the
  first creation. No per-version work.
- **Hook installation fails / discovery never resolves**: nothing is ever
  patched; discovery + caching keep running; the log says exactly what was
  searched and why candidates were rejected.

## Config (`config/d3d9_uiscale.ini`)

The shipped ini is deliberately minimal — three user-facing settings:

- `[Master] Enabled` — master switch.
- `[Features] Scale` — `0` = auto (`min(bb/1920, bb/1080)`, clamped ≥ 1 →
  1× at 1080p, 2× at 4K); any other value = explicit uniform multiplier.
- `[Features] Borderless` — borderless windowed at monitor resolution
  (default ON; renders above the game's 1080p cap and feeds the auto scale).
- `[Debug] Cache / Logging / Verbose` — diagnostics (logging off by default;
  turning `Logging=1` on is the first support step).

Advanced keys (undocumented in the shipped file, still honored if re-added,
defaults fit every known edition): `[Scaling] Apply/ReferenceWidth/
ReferenceHeight`, `[Borderless] Width/Height`, `[Gate] StableFrames/
StableFramesPlaceholder`, `[Discovery] StableFrames/TimeoutFrames/
SlotsPerTick/MaxRectOffset/MinCanvasXref`, `[Signatures] GuardA/PrologueA/
GuardB/PrologueB`.

## Build

32-bit only (RCT3 is x86). Configure with the Win32 preset; the target emits
`d3d9.dll`. Deploy it plus `config/d3d9_uiscale.ini` next to the game exe.
