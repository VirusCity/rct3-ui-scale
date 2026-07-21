// hooks/superwide_fix.h — super-ultrawide (>~2.78:1 / 32:9) crash fix.
//
// RCT3's UI quad allocator returns a vertex-buffer slot pointer, or NULL when
// the engine's clipper rejects a quad (fully off-screen / degenerate). Its
// callers — the loading-progress bar and 30+ other UI builders — write to that
// pointer WITHOUT a null check. Past aspect ~1280/460 = 2.7826 (~25:9; e.g. 32:9
// displays) the fixed-reference loading screen pushes its bar off-screen, the
// clipper rejects it, the allocator returns NULL, and the unchecked write hits
// address 0 → crash on launch. See research/ghidra_notes.md.
//
// This hooks the allocator and, on a NULL return, hands back a throwaway scratch
// buffer: the rejected quad was never added to the draw batch, so nothing
// renders, but no caller ever dereferences NULL. It is the root-cause fix — one
// hook covers every screen and every unchecked caller, needs no canvas discovery
// or cache (works on the very first launch), and never touches gameplay layout.
// Located per edition by the allocator's pointer-return epilogue (timing-only
// signature, INI-overridable); a no-op on builds whose signature doesn't match.
#pragma once

struct IDirect3DDevice9;

namespace superwide {

// Locate + hook the UI quad allocator. Call once at attach; self-gates on
// [Borderless] SuperUltrawideFix (default ON) and logs what it did.
void Install();

// Call from the Present hook, BEFORE the real Present. When the loading screen
// was pillarboxed this frame, fills the margin with [Borderless]
// SuperUltrawideFillColor — the previous screen keeps rendering behind the
// loading screen, so without this it shows through beside the box. A no-op
// on every other frame.
void OnPresent(IDirect3DDevice9* dev);

}  // namespace superwide
