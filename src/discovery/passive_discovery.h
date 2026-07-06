// discovery/passive_discovery.h — signature-less, hook-less canvas discovery.
//
// The source of truth for WHERE the canvas lives. Finds the {global slot RVA,
// rect offset} mapping purely by data flow:
//
//   * Scan the exe's writable data sections for pointer slots whose target
//     object contains a float LTRB rect [~0, ~0, bbW, bbH] — i.e. derive the
//     global slot and the rect offset together (pointer derivation).
//   * Temporal coherence: a candidate must keep matching the backbuffer, at
//     the same object address, across N consecutive ticks.
//   * Structural stability: an object address that churns between ticks is a
//     transient allocation — its stability counter resets.
//
// Multiple stable candidates can survive (D3D wrapper globals also hold
// device-sized rects — the build-20 decoys). Disambiguation is data-flow too:
// a candidate is CONFIRMED when its slot value changes across a creator-hook
// execution and immediately resolves to a fresh device-sized rect (the
// creator provably wrote it). A candidate that is the ONLY stable one after a
// full scan pass is accepted on uniqueness. Anything still ambiguous is never
// scaled — logged and left alone.
//
// Runs exclusively on the render thread (Present tick), except
// OnCreatorFired(), which the timing hook calls from the game thread; the
// candidate table is lock-protected for exactly that crossing.
#pragma once

#include <cstdint>

namespace passive {

void Init();

// Seed the table with the cache's mapping — it is re-verified like any other
// candidate before being trusted (never blindly).
void SeedFromCache(uint32_t slotRva, uint32_t rectOff);

// Per-frame tick from the gate (render thread, stable backbuffer only).
void Tick(unsigned bbW, unsigned bbH);

// Backbuffer changed (device reset / mode change): every candidate must
// re-prove itself against the new dimensions. Cached/accepted mappings are
// re-verified before they are ever discarded.
void OnBackbufferChanged(unsigned newW, unsigned newH);

// Called by a creator detour BEFORE the original runs. Always captures the
// published mapping slot's pre-pointer (to prove a canvas (re)creation this
// call). When takeSnapshot is true (discovery active) it also snapshots the
// exe's writable data so OnCreatorFired can diff exactly which global slots
// that call wrote (pure data-flow write attribution — no watchpoints).
void PreCreator(bool takeSnapshot);

// Discard a snapshot taken by PreCreator when the detour decides not to run
// attribution after all (keeps the snapshot slot from wedging).
void DropSnapshot();

// Called by a creator detour AFTER the original ran (game thread).
// hookSlotRva = the slot this hook was derived from (0 for guard-tier hooks
// that are not tied to a slot). Returns true with the mapping to scale NOW —
// i.e. a published-and-validated mapping, or a candidate this very creator
// call just confirmed (accepted + published + cached inside).
bool OnCreatorFired(uint32_t hookSlotRva, unsigned bbW, unsigned bbH,
                    uint32_t* outSlotRva, uint32_t* outRectOff);

}  // namespace passive
