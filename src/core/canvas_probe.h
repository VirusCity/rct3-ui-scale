// core/canvas_probe.h — fault-proof canvas resolution + validation.
//
// The one shared implementation of the refactor's rule 2: consumers pass a
// {slot RVA, rect offset} mapping and get a FRESHLY resolved rect back —
//     canvas = *(imageBase + slotRva);   rect = canvas + rectOff;
// with every dereference VirtualQuery-guarded so a stale or hostile value can
// never fault. Nothing here caches a resolved pointer.
#pragma once

#include <cstdint>

namespace probe {

// What a resolved mapping looks like against the current backbuffer.
enum class RectStatus {
  Dormant,     // slot committed but null — canvas not built right now (benign)
  Unscaled,    // rect == [~0, ~0, bbW, bbH] exactly — fresh, eligible to shrink
  Scaled,      // rect == backbuffer / S for the current uniform scale S
  Mismatch,    // slot/object resolves but the rect matches nothing expected
  Unreadable,  // slot / object / rect memory not safely readable
};

struct Resolved {
  RectStatus status;
  uintptr_t rectVA;  // valid only for Unscaled/Scaled/Mismatch
  float l, t, r, b;  // rect values as read (Unscaled/Scaled/Mismatch)
};

// True iff [p, p+len) is committed, readable, non-guard, in one region.
bool Readable(uintptr_t p, size_t len);

// Copy len bytes from p if Readable (and SEH-guarded against races with a
// concurrent free). Returns false on any failure.
bool SafeRead(uintptr_t p, void* out, size_t len);

// Resolve slotRva/rectOff freshly and classify the rect against bbW/bbH and
// the current uniform scale. imageBase 0 = use the exe module.
Resolved Resolve(uint32_t slotRva, uint32_t rectOff, unsigned bbW, unsigned bbH);

// Image bounds of the game exe (resolved once, immutable afterwards).
uintptr_t ImageBase();
uintptr_t ImageEnd();

}  // namespace probe
