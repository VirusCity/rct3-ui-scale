// source_patch.* — in-memory patches to the game's own UI layout, applied at
// startup. The "secondary strategy" from CLAUDE.md, now under live test.
//
// Hypothesis (see research/ghidra_notes.md, "GUI2 scale system"): every GUI2 UI
// element stores a per-element scale float at `element + 0xF0`, written to 1.0
// in ~12 element constructors and consumed by the layout that feeds BOTH the
// renderer and the hit-test. If true, raising that default to our scale enlarges
// the whole UI *and* keeps clicks aligned natively — the clean fix the render-
// side hack can't give us.
//
// This is an EXPERIMENT: it does not hardcode addresses (CLAUDE.md hard rule).
// It signature-scans the live RCT3.exe image for the constructor instruction
//   mov dword ptr [reg + 0xF0], 1.0f   (C7 /0  ModRM  disp32=0xF0  imm32=1.0f)
// and rewrites the 1.0f immediate to the configured scale. Gated behind
// [SourcePatch] Enabled and fully logged so the in-game result is unambiguous.
#pragma once

namespace sourcepatch {

// Scan the main executable for the GUI2 "+0xF0 = 1.0f" constructor defaults and
// rewrite the immediate to `scale`. Call ONCE at DLL attach, before any UI
// element is constructed. No-op (returns 0) when scale == 1.0. Returns the
// number of sites patched. Safe to call under loader lock (pure memory work).
int ApplyGui2ScaleDefault(float scale);

}  // namespace sourcepatch
