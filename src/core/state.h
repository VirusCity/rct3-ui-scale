// core/state.h — thread-safe shared system state.
//
// Discovery runs on the render thread (Present tick); the timing hooks run on
// whatever thread the game builds its UI from. Everything they share lives
// here as atomics so the hook only ever observes fully-initialized state:
//  * the system State (Discovering / Ready / Failed),
//  * the published canvas mapping {global slot RVA, rect offset} — packed into
//    ONE 64-bit atomic so a reader can never see a torn pair,
//  * the current backbuffer dimensions (packed the same way),
//  * a creator-run serial used to correlate slot writes with creator calls.
//
// Only the RVA + offset are ever stored. Resolved pointers are NEVER cached
// anywhere — every consumer re-resolves canvas = *(slot), rect = canvas + off
// at each use (the refactor's rule 2).
#pragma once

#include <cstdint>

enum class State : int {
  Discovering = 0,  // no validated mapping for the current backbuffer
  Ready = 1,        // mapping published + validated; hook may scale
  Failed = 2,       // revalidation timed out — do nothing until new evidence
};

namespace state {

State Get();
void Set(State s);

// Canvas mapping. slotRva is the RVA (exe-relative) of the writable global
// holding the canvas object pointer; rectOff the rect's offset inside that
// object. Publish stores both atomically; Get returns false while unset.
void PublishMapping(uint32_t slotRva, uint32_t rectOff);
bool GetMapping(uint32_t* slotRva, uint32_t* rectOff);
void ClearMapping();

// Current backbuffer dimensions, updated by the device hooks (CreateDevice /
// Reset / Present). The timing hook validates against these — they are set
// BEFORE the game can run a creator on the new device.
void SetBackbuffer(unsigned w, unsigned h);
bool GetBackbuffer(unsigned* w, unsigned* h);

// Creator-run correlation: each creator-detour execution bumps the serial.
// Discovery compares serials across ticks to attribute slot-value changes to
// a creator run.
void NoteCreatorRun();
uint32_t CreatorSerial();

// Set once a shrink has actually been applied this session (crash-cookie
// bookkeeping: the selector clears the on-disk cookie on the first tick that
// observes this, proving the process survived the patched creation).
void NoteScaled();
bool ScaledSinceAttach();

}  // namespace state
