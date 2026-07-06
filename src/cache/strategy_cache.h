// cache/strategy_cache.h — persisted discovery results (CLAUDE.md Caching).
//
// Fingerprint: invariant PE-header facts of the game exe (TimeDateStamp,
// SizeOfImage, entry RVA, section count, file size) folded through FNV-1a.
// Deliberately NOT a .text hash — community-patched / cracked / widescreen-
// fixed executables must keep their cache (they are the point of this mod).
// A fingerprint miss is harmless: it just forces rediscovery.
//
// Persisted (per exe fingerprint) — ONLY relocatable values, never resolved
// pointers or absolute addresses:
//  * CONFIRMED mapping: the canvas global-slot RVA + rect offset, once
//    discovery has attributed the slot to a creator (or it was unique).
//  * PENDING candidates: stable-but-ambiguous candidate mappings from a run
//    that could not disambiguate. The next run arms store-derived timing
//    hooks on ALL of them before the game builds anything, so the very first
//    creation attributes the write and confirms the real one.
//  * ARMED cookie: set while a session is armed to scale; cleared on the
//    first Present after a successful shrink (and on clean detach). Finding
//    it set at attach means the last session died after arming — the cache
//    is discarded rather than trusted into a crash loop.
#pragma once

namespace cache {

struct Entry {
  unsigned globalSlotRva = 0;    // exe .data RVA of the canvas-pointer slot
  unsigned canvasRectOffset = 0; // rect offset within the canvas object
};

void Init(const char* directory);  // cache file lives next to the DLL

// CONFIRMED mapping. Load returns true only on a fingerprint match.
bool Load(Entry* out);
void StoreCatchSites(unsigned globalSlotRva, unsigned canvasRectOffset);
void InvalidateStored();  // drop confirmed + pending (bad cache / cookie hit)

// PENDING candidate list (fingerprint-checked like Load).
int LoadPending(Entry* out, int maxOut);
void StorePending(const Entry* list, int count);

// ARMED crash cookie.
bool ArmedCookieSet();
void WriteArmedCookie(bool set);

}  // namespace cache
