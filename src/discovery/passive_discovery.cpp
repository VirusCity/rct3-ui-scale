#include "passive_discovery.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "../cache/strategy_cache.h"
#include "../core/canvas_probe.h"
#include "../core/config.h"
#include "../core/log.h"
#include "../core/state.h"
#include "../hooks/timing_hook.h"

namespace passive {
namespace {

constexpr int kMaxCands = 64;
constexpr int kDropAfterMisses = 900;   // proven candidate stops proving: forget
constexpr int kDropNeverStable = 120;   // one-shot garbage match: forget fast
constexpr int kLogEvery = 600;          // periodic heartbeat while discovering
constexpr int kRescanIdleTicks = 120;   // pause between scan passes after pass 0

struct Cand {
  uint32_t slotRva = 0;
  uint32_t rectOff = 0;
  uintptr_t lastObj = 0;  // last observed object (observation only, never patched)
  int stable = 0;         // consecutive ticks valid at the same object
  int miss = 0;           // consecutive ticks mismatched/unreadable
  bool fromCache = false;
  bool hookArmed = false;
  bool confirmed = false;     // accepted (this is/was the published mapping)
  bool multiOffset = false;   // object matched at >1 offset — never auto-accept
  bool everStable = false;    // re-proved at least once after admission
  bool storeChecked = false;  // exe-store discriminator evaluated
  bool hasStore = false;      // a game-code store targets this slot (real global)
  int xref = -1;              // #code refs to this global (-1 = not computed)
  uint32_t bornPass = 0;
};

struct Range {
  uintptr_t start = 0;
  uintptr_t end = 0;
};

// The candidate table is written by the render thread (Tick) and read/written
// by the game thread (OnCreatorFired) — everything below g_lock is shared.
SRWLOCK g_lock = SRWLOCK_INIT;
Cand g_cands[kMaxCands];
int g_candCount = 0;

Range g_ranges[8];
int g_rangeCount = 0;
size_t g_totalBytes = 0;

// Scan cursor (render thread only).
int g_curRange = 0;
uintptr_t g_curPos = 0;
uint32_t g_pass = 0;  // completed full passes over the writable ranges
int g_nextScanTick = 0;      // pass >= 1: idle until this tick before rescanning
// Diagnostics (cumulative; reported in the heartbeat, never per-hit — a 4K
// menu legitimately holds hundreds of [0,0,bbW,bbH] widget rects).
unsigned g_multiOffsetSkips = 0;
unsigned g_tableOverflow = 0;

unsigned g_bbW = 0, g_bbH = 0;

// Revalidation bookkeeping (spec section 5).
bool g_revalidating = false;
int g_sinceChange = 0;
int g_mappingMiss = 0;
int g_revalFrames = 0;  // consecutive ticks the published mapping re-matched
int g_ticks = 0;
uint32_t g_lastUnscaledLogSerial = 0;

// Creator-call snapshot for write attribution: a copy of every writable exe
// data range, taken just before the original creator runs. Diffing it after
// the call yields exactly the global slots that call wrote — the pure
// data-flow equivalent of a hardware write-watch.
uint8_t* g_snapBuf = nullptr;
std::atomic<int> g_snapState{0};  // 0 = idle, 1 = taken (valid for one diff)

bool g_pendingConfirmedWrite = false;
bool g_pendingListDirty = false;
int g_lastPendingWriteTick = 0;

void EnumWritableRanges() {
  const uintptr_t base = probe::ImageBase();
  if (!base) return;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  const auto* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0;
       i < nt->FileHeader.NumberOfSections && g_rangeCount < 8; ++i, ++sec) {
    const DWORD ch = sec->Characteristics;
    if (!(ch & IMAGE_SCN_MEM_WRITE)) continue;
    if (ch & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_DISCARDABLE)) continue;
    Range& r = g_ranges[g_rangeCount++];
    r.start = base + sec->VirtualAddress;
    const DWORD size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize
                                             : sec->SizeOfRawData;
    r.end = (r.start + size) & ~static_cast<uintptr_t>(3);
    g_totalBytes += r.end - r.start;
    LOG("passive: writable data range %.8s [%p, %p) (%u KB)",
        sec->Name, reinterpret_cast<void*>(r.start),
        reinterpret_cast<void*>(r.end),
        static_cast<unsigned>((r.end - r.start) / 1024));
  }
}

// Plausible pointer to a heap object (canvas objects live outside the image).
bool PlausibleObjectPtr(uintptr_t v) {
  return v >= 0x10000 && (v & 3) == 0 &&
         !(v >= probe::ImageBase() && v < probe::ImageEnd());
}

// SEH-guarded copy WITHOUT its own VirtualQuery — callers have already
// classified the region; the guard only covers the query-to-copy race.
bool TryCopy(void* dst, const void* src, size_t len) {
  __try {
    memcpy(dst, src, len);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// Tiny per-tick region cache: the scan probes thousands of pointers per tick
// and they cluster into a handful of heap regions — one VirtualQuery per
// region instead of two per pointer. Reset every tick (bounded staleness;
// TryCopy absorbs the rest).
struct RegionInfo {
  uintptr_t base = 0, end = 0;
  bool ok = false;
};
RegionInfo g_regCache[4];
int g_regNext = 0;

void ResetRegionCache() {
  for (RegionInfo& r : g_regCache) r = RegionInfo{};
  g_regNext = 0;
}

bool RegionReadable(uintptr_t p, uintptr_t* endOut) {
  for (const RegionInfo& r : g_regCache) {
    if (p >= r.base && p < r.end) {
      *endOut = r.end;
      return r.ok;
    }
  }
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi)) !=
      sizeof(mbi))
    return false;
  RegionInfo ri;
  ri.base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
  ri.end = ri.base + mbi.RegionSize;
  const DWORD prot = mbi.Protect & 0xFF;
  // MEM_PRIVATE only: the canvas is a heap object. Requiring private memory
  // rejects string/constant bytes chased as pointers into mapped image/file
  // regions (the "pqrs"/"Dayl" false positives) — the dominant scan noise.
  ri.ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
          !(mbi.Protect & PAGE_GUARD) &&
          (prot == PAGE_READONLY || prot == PAGE_READWRITE ||
           prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);
  g_regCache[g_regNext++ & 3] = ri;
  *endOut = ri.end;
  return ri.ok;
}

// Scan an object's first maxOff bytes for a float LTRB rect matching the
// backbuffer: [~0, ~0, bbW, bbH]. Returns the number of matching offsets
// found (0..2) and the first in *off.
int FindRectInObject(uintptr_t obj, unsigned bbW, unsigned bbH,
                     uint32_t* off) {
  const int maxOff = GetConfig().maxRectOffset;
  uintptr_t regEnd = 0;
  if (!RegionReadable(obj, &regEnd)) return 0;
  if (obj + 16 > regEnd) return 0;
  size_t window = static_cast<size_t>(maxOff) + 16;
  if (obj + window > regEnd) window = regEnd - obj;

  static thread_local uint8_t buf[0x400 + 16];
  if (window > sizeof(buf)) window = sizeof(buf);
  if (!TryCopy(buf, reinterpret_cast<const void*>(obj), window)) return 0;

  const float fbW = static_cast<float>(bbW);
  const float fbH = static_cast<float>(bbH);
  int found = 0;
  for (size_t o = 0; o + 16 <= window; o += 4) {
    float r[4];
    memcpy(r, buf + o, 16);
    if (std::fabs(r[0]) > 0.5f || std::fabs(r[1]) > 0.5f) continue;
    const float w = r[2] - r[0], h = r[3] - r[1];
    if (std::fabs(w - fbW) > 0.5f || std::fabs(h - fbH) > 0.5f) continue;
    if (found == 0 && off) *off = static_cast<uint32_t>(o);
    if (++found >= 2) break;  // two is already "ambiguous within the object"
  }
  return found;
}

Cand* FindCandLocked(uint32_t slotRva) {
  for (int i = 0; i < g_candCount; ++i)
    if (g_cands[i].slotRva == slotRva) return &g_cands[i];
  return nullptr;
}

// Evict the weakest junk seat (never a confirmed / cache-seeded candidate).
// allowProven=false protects everStable seats (a scan hit must not displace a
// candidate that has proved itself — this is how the real slot survives once
// found); allowProven=true lets decisive attribution evidence take any seat.
bool EvictWeakestLocked(bool allowProven) {
  int worst = -1;
  for (int i = 0; i < g_candCount; ++i) {
    const Cand& c = g_cands[i];
    if (c.confirmed || c.fromCache) continue;
    if (worst < 0 || c.stable < g_cands[worst].stable) worst = i;
  }
  if (worst < 0) return false;
  if (!allowProven && g_cands[worst].everStable) return false;
  g_cands[worst] = g_cands[--g_candCount];
  return true;
}

Cand* AddCandLocked(uint32_t slotRva, uint32_t rectOff, uintptr_t obj,
                    bool fromCache, bool multiOffset) {
  if (Cand* c = FindCandLocked(slotRva)) return c;
  if (g_candCount >= kMaxCands) {
    // Full: evict the weakest UNPROVEN seat so a fresh scan hit still gets a
    // chance (a proven/everStable candidate is never displaced by an unproven
    // one — that is how the real slot survives once found). No per-hit
    // logging: a 4K park holds many legit full-screen widget rects.
    ++g_tableOverflow;
    if (!EvictWeakestLocked(/*allowProven=*/false))
      return nullptr;  // table is all proven — leave it be
  }
  Cand& c = g_cands[g_candCount++];
  c = Cand{};
  c.slotRva = slotRva;
  c.rectOff = rectOff;
  c.lastObj = obj;
  c.stable = obj ? 1 : 0;
  c.fromCache = fromCache;
  c.multiOffset = multiOffset;
  c.bornPass = g_pass;
  g_pendingListDirty = true;
  if (GetConfig().logVerbose || fromCache)
    LOG("passive: candidate slot exe+0x%X -> obj %p rect+0x%X%s%s (pass %u, "
        "%d total)",
        slotRva, reinterpret_cast<void*>(obj), rectOff,
        fromCache ? " [cache]" : "", multiOffset ? " [MULTI-OFFSET]" : "",
        g_pass, g_candCount);
  return &c;
}

// Accept a candidate: publish the mapping (atomically), mark Ready, queue the
// cache write. Lock must be held.
void AcceptLocked(Cand& c, const char* how) {
  c.confirmed = true;
  state::PublishMapping(c.slotRva, c.rectOff);
  state::Set(State::Ready);
  g_revalidating = false;
  g_mappingMiss = 0;
  g_pendingConfirmedWrite = true;
  LOG("passive: ACCEPTED canvas mapping slot exe+0x%X rect+0x%X (%s) — "
      "state Ready",
      c.slotRva, c.rectOff, how);
}

// --- creator-call write attribution ---------------------------------------

// Pre-call pointer of the published mapping slot, captured by PreCreator on
// EVERY creator fire. The shrink only proceeds when this changes across the
// call — proof the creator (re)created the canvas THIS call, i.e. we are
// before layout, never patching an already-laid-out canvas.
uintptr_t g_preSlotPtr = 0;
bool g_preSlotValid = false;

void CapturePreSlot() {
  g_preSlotValid = false;
  uint32_t slot, off;
  if (!state::GetMapping(&slot, &off)) return;
  uintptr_t p = 0;
  if (probe::SafeRead(probe::ImageBase() + slot, &p, sizeof(p))) {
    g_preSlotPtr = p;
    g_preSlotValid = true;
  }
}

void TakeSnapshot() {
  if (!g_snapBuf || !g_rangeCount) return;
  int expected = 0;
  if (!g_snapState.compare_exchange_strong(expected, 2))  // 2 = in progress
    return;  // another creator call is mid-flight — its diff wins
  uint8_t* p = g_snapBuf;
  for (int i = 0; i < g_rangeCount; ++i) {
    memcpy(p, reinterpret_cast<const void*>(g_ranges[i].start),
           g_ranges[i].end - g_ranges[i].start);
    p += g_ranges[i].end - g_ranges[i].start;
  }
  g_snapState.store(1);
}

// Diff the snapshot against live memory: every changed slot whose NEW value
// points at an object holding an unscaled backbuffer rect was written by the
// creator call that just returned. Returns count; fills up to maxOut entries.
struct Written {
  uint32_t slotRva;
  uint32_t rectOff;
  uintptr_t obj;
  bool multiOffset;
};
int DiffSnapshot(unsigned bbW, unsigned bbH, Written* out, int maxOut) {
  int expected = 1;
  if (!g_snapState.compare_exchange_strong(expected, 2)) return -1;  // no snap
  int n = 0;
  const uint8_t* p = g_snapBuf;
  for (int i = 0; i < g_rangeCount; ++i) {
    const uintptr_t start = g_ranges[i].start;
    const size_t bytes = g_ranges[i].end - start;
    const uint32_t* oldW = reinterpret_cast<const uint32_t*>(p);
    const uint32_t* newW = reinterpret_cast<const uint32_t*>(start);
    const size_t words = bytes / 4;
    for (size_t w = 0; w < words; ++w) {
      const uint32_t nv = newW[w];
      if (nv == oldW[w]) continue;
      if (!PlausibleObjectPtr(nv)) continue;
      uint32_t off = 0;
      const int matches = FindRectInObject(nv, bbW, bbH, &off);
      if (!matches) continue;
      if (n < maxOut) {
        out[n].slotRva =
            static_cast<uint32_t>(start + w * 4 - probe::ImageBase());
        out[n].rectOff = off;
        out[n].obj = nv;
        out[n].multiOffset = matches > 1;
      }
      ++n;
    }
    p += bytes;
  }
  g_snapState.store(0);
  return n;
}

void DropSnapshotIfAny() {
  int expected = 1;
  g_snapState.compare_exchange_strong(expected, 0);
}

// --- per-tick candidate maintenance ----------------------------------------

void UpdateCandidatesLocked() {
  for (int i = 0; i < g_candCount;) {
    Cand& c = g_cands[i];
    const probe::Resolved r = probe::Resolve(c.slotRva, c.rectOff, g_bbW, g_bbH);
    switch (r.status) {
      case probe::RectStatus::Unscaled:
      case probe::RectStatus::Scaled: {
        const uintptr_t obj = r.rectVA - c.rectOff;
        if (obj == c.lastObj) {
          if (c.stable < 0x40000000) ++c.stable;
          if (c.stable >= 2) c.everStable = true;
        } else {
          // Structural churn: a NEW object behind the same slot must re-prove
          // itself (reject transient allocations).
          c.lastObj = obj;
          c.stable = 1;
        }
        c.miss = 0;
        break;
      }
      case probe::RectStatus::Dormant:
        // Canvas torn down (e.g. desktop destroyed) — benign, not a strike,
        // but the candidate is no longer proven for acceptance purposes.
        c.stable = 0;
        c.lastObj = 0;
        c.miss = 0;
        break;
      default:  // Mismatch / Unreadable
        c.stable = 0;
        ++c.miss;
        break;
    }
    // One-shot garbage (never re-proved after admission) is dropped fast;
    // a candidate that once proved itself gets the long window.
    const int dropLimit = c.everStable ? kDropAfterMisses : kDropNeverStable;
    if (c.miss > dropLimit && !c.confirmed && !c.fromCache) {
      g_cands[i] = g_cands[--g_candCount];
      g_pendingListDirty = true;
      continue;
    }
    ++i;
  }
}

void ScanStepLocked() {
  if (!g_rangeCount) return;
  // Full speed until the first pass completes (startup discovery latency),
  // then idle between passes — known candidates are re-validated every tick
  // anyway; a rescan only needs to notice NEW slots occasionally.
  const bool atStart = (g_curRange == 0 && g_curPos == 0);
  if (g_pass >= 1 && atStart && g_ticks < g_nextScanTick) return;

  int budget = GetConfig().slotsPerTick;
  while (budget > 0) {
    if (g_curRange >= g_rangeCount) {
      g_curRange = 0;
      g_curPos = 0;
      ++g_pass;
      g_nextScanTick = g_ticks + kRescanIdleTicks;
      return;  // one wrap per tick max
    }
    Range& r = g_ranges[g_curRange];
    if (!g_curPos) g_curPos = r.start;
    while (g_curPos + 4 <= r.end && budget > 0) {
      const uint32_t v = *reinterpret_cast<const uint32_t*>(g_curPos);
      const uintptr_t slotVA = g_curPos;
      g_curPos += 4;
      --budget;
      if (!PlausibleObjectPtr(v)) continue;
      uint32_t off = 0;
      const int matches = FindRectInObject(v, g_bbW, g_bbH, &off);
      if (!matches) continue;
      // Seat multi-rect objects too — the canvas root itself may carry more
      // than one device-sized rect. multiOffset only bars UNIQUENESS
      // auto-accept; attribution / store-hook can still confirm it.
      if (matches > 1) ++g_multiOffsetSkips;  // (diagnostic tally only)
      AddCandLocked(static_cast<uint32_t>(slotVA - probe::ImageBase()), off, v,
                    /*fromCache=*/false, matches > 1);
    }
    if (g_curPos + 4 > r.end) {
      ++g_curRange;
      g_curPos = 0;
    }
  }
}

// Spec section 5: while Discovering with a published mapping (cached or
// demoted), count consecutive ticks the mapping re-matches the backbuffer;
// a single-frame mismatch only resets the counter. N matches => Ready again
// without any rediscovery.
void RevalidateMappingLocked() {
  uint32_t slot, off;
  if (!state::GetMapping(&slot, &off)) return;
  const probe::Resolved r = probe::Resolve(slot, off, g_bbW, g_bbH);
  if (r.status == probe::RectStatus::Unscaled ||
      r.status == probe::RectStatus::Scaled) {
    if (++g_revalFrames >= GetConfig().discStableFrames) {
      LOG("passive: published mapping exe+0x%X/+0x%X re-matched the "
          "backbuffer for %d ticks — state Ready",
          slot, off, g_revalFrames);
      state::Set(State::Ready);
      g_revalidating = false;
      g_revalFrames = 0;
      g_mappingMiss = 0;
      if (Cand* c = FindCandLocked(slot)) c->confirmed = true;
    }
  } else {
    g_revalFrames = 0;  // Dormant/Mismatch/Unreadable: not evidence either way
  }
}

// Uniqueness acceptance: after at least one full pass, if exactly one
// candidate exists and it has been stable >= N ticks, it IS the canvas —
// there is nothing to disambiguate.
void TryUniquenessAcceptLocked() {
  if (g_pass < 1 || g_candCount != 1) return;
  Cand& c = g_cands[0];
  if (c.confirmed || c.multiOffset) return;
  if (c.stable < GetConfig().discStableFrames) return;
  AcceptLocked(c, "unique candidate after full scan pass");
}

// Xref-dominance acceptance (primary on real engines): among stable real
// globals, the canvas is the one referenced by hundreds of code sites — the
// decoy device-rect globals are referenced by a handful. Accept the top-xref
// candidate when it clears the floor AND dominates the runner-up. This does
// NOT scale anything; it publishes + caches the mapping. The actual shrink
// still waits for a creator to (re)create the canvas (pointer-change gated),
// so a device-sized-but-already-laid-out canvas is never touched.
void TryXrefAcceptLocked() {
  int best = -1, second = -1;
  for (int i = 0; i < g_candCount; ++i) {
    const Cand& c = g_cands[i];
    if (c.confirmed || !c.hasStore || c.xref < 0) continue;
    if (c.stable < GetConfig().discStableFrames) continue;
    if (best < 0 || c.xref > g_cands[best].xref) {
      second = best;
      best = i;
    } else if (second < 0 || c.xref > g_cands[second].xref) {
      second = i;
    }
  }
  if (best < 0) return;
  const int bx = g_cands[best].xref;
  const int sx = second >= 0 ? g_cands[second].xref : 0;
  if (bx < GetConfig().minCanvasXref) return;   // not canvas-like
  if (sx * 2 > bx) return;                       // not dominant — wait/ambiguous
  LOG("passive: canvas by xref dominance — slot exe+0x%X rect+0x%X has %d "
      "code refs vs runner-up %d (>=2x); confirming as the UI root",
      g_cands[best].slotRva, g_cands[best].rectOff, bx, sx);
  AcceptLocked(g_cands[best], "xref dominance (UI-root reference count)");
}

// Arm store-derived timing hooks for stable-but-ambiguous candidates so the
// next creator run can attribute the write. Collect outside the lock — hook
// installation suspends threads.
int CollectHookWorkLocked(uint32_t* outSlots, int maxOut) {
  int n = 0;
  for (int i = 0; i < g_candCount && n < maxOut; ++i) {
    Cand& c = g_cands[i];
    if (c.hookArmed || c.stable < GetConfig().discStableFrames) continue;
    c.hookArmed = true;  // one attempt per candidate
    outSlots[n++] = c.slotRva;
  }
  return n;
}

// Persist the current (unconfirmed) candidate set so the NEXT run can arm
// store hooks before the game creates anything, and attribute the very first
// creation. Only stable candidates are worth persisting; only write when the
// list content actually changed. Render thread only.
cache::Entry g_lastPendingList[kMaxCands];
int g_lastPendingCount = -1;  // -1 = never written this session

void MaybeWritePendingLocked() {
  if (!g_pendingListDirty || state::Get() == State::Ready) return;
  if (!GetConfig().cacheEnabled) return;
  if (g_ticks - g_lastPendingWriteTick < 120) return;  // throttle: ~2s
  cache::Entry list[kMaxCands];
  int n = 0;
  for (int i = 0; i < g_candCount && n < kMaxCands; ++i) {
    // Real game-written globals proven this session, plus everything seeded
    // from the cache (proven by a PREVIOUS session — a run in which the
    // canvas never gets built must not erase old calibration).
    const bool provenNow = g_cands[i].everStable && g_cands[i].hasStore;
    if (!provenNow && !g_cands[i].fromCache) continue;
    list[n].globalSlotRva = g_cands[i].slotRva;
    list[n].canvasRectOffset = g_cands[i].rectOff;
    ++n;
  }
  g_pendingListDirty = false;
  g_lastPendingWriteTick = g_ticks;
  if (n == 0) return;  // never persist an empty list over prior knowledge
  if (n == g_lastPendingCount &&
      memcmp(list, g_lastPendingList, n * sizeof(cache::Entry)) == 0)
    return;  // unchanged — don't churn the file
  memcpy(g_lastPendingList, list, n * sizeof(cache::Entry));
  g_lastPendingCount = n;
  cache::StorePending(list, n);
}

// Ready-state health monitor (spec section 5): tolerate single-frame
// mismatches; a sustained mismatch demotes to Discovering (the mapping stays
// seeded — re-verified before it is ever discarded).
void MonitorReadyLocked() {
  uint32_t slot, off;
  if (!state::GetMapping(&slot, &off)) return;
  const probe::Resolved r = probe::Resolve(slot, off, g_bbW, g_bbH);
  switch (r.status) {
    case probe::RectStatus::Scaled:
    case probe::RectStatus::Dormant:
      g_mappingMiss = 0;
      break;
    case probe::RectStatus::Unscaled: {
      // Canvas rebuilt at device size and the timing hook did NOT catch it.
      // There is no post-layout fallback — never scale here. Log once per
      // creator generation so a missing hook is visible in the log.
      g_mappingMiss = 0;
      const uint32_t serial = state::CreatorSerial();
      if (g_lastUnscaledLogSerial != serial) {
        g_lastUnscaledLogSerial = serial;
        LOG("passive: canvas is device-sized but no timing hook fired — NOT "
            "scaling (no post-layout path). Check hook installation lines "
            "above.");
      }
      break;
    }
    default:
      if (++g_mappingMiss > GetConfig().discTimeoutFrames) {
        LOG("passive: published mapping exe+0x%X/+0x%X failed revalidation "
            "for %d ticks — demoting to Discovering (mapping kept seeded)",
            slot, off, g_mappingMiss);
        g_mappingMiss = 0;
        state::Set(State::Discovering);
        g_revalidating = true;
        g_sinceChange = 0;
      }
      break;
  }
}

}  // namespace

void Init() {
  EnumWritableRanges();
  if (g_totalBytes) {
    g_snapBuf = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, g_totalBytes, MEM_COMMIT | MEM_RESERVE,
                     PAGE_READWRITE));
    if (!g_snapBuf)
      LOG("passive: snapshot buffer alloc (%zu KB) failed — creator write "
          "attribution disabled; only uniqueness/store-hook acceptance",
          g_totalBytes / 1024);
  }
  LOG("passive: init — %d writable range(s), %zu KB, snapshot %s",
      g_rangeCount, g_totalBytes / 1024, g_snapBuf ? "armed" : "OFF");
}

void SeedFromCache(uint32_t slotRva, uint32_t rectOff) {
  if (!slotRva || !rectOff) return;
  AcquireSRWLockExclusive(&g_lock);
  Cand* c = AddCandLocked(slotRva, rectOff, 0, /*fromCache=*/true, false);
  if (c) {
    c->hookArmed = true;  // the selector arms cache hooks itself
    // A pending entry was persisted BECAUSE it was a stable real global last
    // run — carry that over, or a menu-only session (candidate dormant, so
    // it can never re-prove) would compute an empty pending list and wipe
    // the previous run's calibration.
    c->storeChecked = true;
    c->hasStore = true;
  }
  ReleaseSRWLockExclusive(&g_lock);
}

void OnBackbufferChanged(unsigned newW, unsigned newH) {
  AcquireSRWLockExclusive(&g_lock);
  g_bbW = newW;
  g_bbH = newH;
  // Every candidate must re-prove against the new dimensions; nothing is
  // discarded here (cached offsets are re-verified before discard).
  for (int i = 0; i < g_candCount; ++i) {
    g_cands[i].stable = 0;
    g_cands[i].miss = 0;
  }
  g_curRange = 0;
  g_curPos = 0;
  g_pass = 0;
  g_revalidating = true;
  g_sinceChange = 0;
  g_mappingMiss = 0;
  if (state::Get() != State::Discovering) {
    state::Set(State::Discovering);
    LOG("passive: backbuffer changed to %ux%u — state Discovering "
        "(revalidation window open)",
        newW, newH);
  }
  ReleaseSRWLockExclusive(&g_lock);
}

void Tick(unsigned bbW, unsigned bbH) {
  if (!bbW || !bbH) return;
  uint32_t hookWork[kMaxCands];
  int hookWorkN = 0;
  bool writeConfirmed = false;
  uint32_t confSlot = 0, confOff = 0;

  AcquireSRWLockExclusive(&g_lock);
  g_bbW = bbW;
  g_bbH = bbH;
  ++g_ticks;
  ++g_sinceChange;
  ResetRegionCache();  // per-tick freshness for the probe fast path

  UpdateCandidatesLocked();

  const State st = state::Get();
  if (st == State::Ready) {
    MonitorReadyLocked();
  } else if (st == State::Discovering) {
    RevalidateMappingLocked();
    ScanStepLocked();
    TryXrefAcceptLocked();
    TryUniquenessAcceptLocked();
    hookWorkN = CollectHookWorkLocked(hookWork, kMaxCands);
    MaybeWritePendingLocked();

    if (g_revalidating && g_sinceChange > GetConfig().discTimeoutFrames &&
        state::Get() == State::Discovering) {
      LOG("passive: revalidation timed out after %d ticks (%d candidate(s), "
          "none accepted) — state Failed. A creator run or backbuffer change "
          "can still recover.",
          g_sinceChange, g_candCount);
      state::Set(State::Failed);
    }
    if ((g_ticks % kLogEvery) == 0) {
      int stableReal = 0;
      for (int i = 0; i < g_candCount; ++i)
        if (g_cands[i].everStable && g_cands[i].hasStore) ++stableReal;
      LOG("passive: discovering… pass=%u cands=%d (stable game-globals=%d) "
          "bb=%ux%u (multi-rect seen=%u, table churn=%u)",
          g_pass, g_candCount, stableReal, g_bbW, g_bbH, g_multiOffsetSkips,
          g_tableOverflow);
    }
  }
  if (g_pendingConfirmedWrite && state::GetMapping(&confSlot, &confOff)) {
    g_pendingConfirmedWrite = false;
    writeConfirmed = true;
  }
  ReleaseSRWLockExclusive(&g_lock);

  // File I/O + hook installation outside the lock (the latter suspends
  // threads; the former can block).
  if (writeConfirmed && GetConfig().cacheEnabled)
    cache::StoreCatchSites(confSlot, confOff);
  for (int i = 0; i < hookWorkN; ++i) {
    const uint32_t slot = hookWork[i];
    // The exe-store count is BOTH the "real game global" discriminator and
    // the source of the creator hooks; the xref count is the canvas
    // discriminator. Both scan code — evaluate once, outside the lock.
    const int stores = timing::SlotStoreSiteCount(slot);
    int xref = -1;
    if (stores > 0) {
      timing::InstallStoreHooksForSlot(slot);
      xref = timing::SlotXrefCount(slot);
    }
    AcquireSRWLockExclusive(&g_lock);
    if (Cand* c = FindCandLocked(slot)) {
      const bool firstReal = stores > 0 && !c->hasStore;
      c->storeChecked = true;
      c->hasStore = stores > 0;
      if (stores > 0) {
        c->xref = xref;
        g_pendingListDirty = true;  // now worth persisting
        if (firstReal)
          LOG("passive: REAL game-global slot exe+0x%X rect+0x%X "
              "(device-sized, stable, %d store site(s), %d code xref(s)) — "
              "canvas candidate, store hooks armed",
              c->slotRva, c->rectOff, stores, xref);
      }
    }
    ReleaseSRWLockExclusive(&g_lock);
  }
}

void PreCreator(bool takeSnapshot) {
  CapturePreSlot();               // always — cheap, one guarded dword read
  if (takeSnapshot) TakeSnapshot();  // only while discovering
}

void DropSnapshot() { DropSnapshotIfAny(); }

bool OnCreatorFired(uint32_t hookSlotRva, unsigned bbW, unsigned bbH,
                    uint32_t* outSlotRva, uint32_t* outRectOff) {
  if (!bbW || !bbH) {
    DropSnapshotIfAny();
    return false;
  }

  AcquireSRWLockExclusive(&g_lock);
  g_bbW = bbW;
  g_bbH = bbH;
  ResetRegionCache();

  // 1) A published mapping that resolves to a fresh device-sized rect is the
  //    normal Ready path — BUT only scale it if THIS creator call just wrote
  //    the slot (its pointer changed since PreCreator). That is the proof we
  //    are pre-layout: a decoy creator firing while the canvas already exists
  //    leaves the slot pointer untouched, so we never shrink a laid-out UI.
  uint32_t slot, off;
  if (state::GetMapping(&slot, &off)) {
    const probe::Resolved r = probe::Resolve(slot, off, bbW, bbH);
    uintptr_t curPtr = 0;
    probe::SafeRead(probe::ImageBase() + slot, &curPtr, sizeof(curPtr));
    const bool createdThisCall =
        g_preSlotValid && curPtr && curPtr != g_preSlotPtr;
    if (r.status == probe::RectStatus::Unscaled && createdThisCall) {
      if (state::Get() != State::Ready) {
        // The creator just re-proved the cached mapping (this IS the
        // re-verification) — recover from Discovering/Failed.
        LOG("passive: creator run re-validated mapping exe+0x%X/+0x%X — "
            "state Ready",
            slot, off);
        state::Set(State::Ready);
        g_revalidating = false;
        g_mappingMiss = 0;
        if (Cand* c = FindCandLocked(slot)) c->confirmed = true;
      }
      DropSnapshotIfAny();
      *outSlotRva = slot;
      *outRectOff = off;
      ReleaseSRWLockExclusive(&g_lock);
      return true;
    }
    if (r.status == probe::RectStatus::Unscaled && !createdThisCall) {
      // Canvas exists device-sized but this call did not create it — do not
      // scale post-layout. (Discovery may still attribute a *different* slot
      // this creator wrote, below.)
      DropSnapshotIfAny();
      ReleaseSRWLockExclusive(&g_lock);
      return false;
    }
    if (r.status == probe::RectStatus::Scaled) {  // already scaled: idempotence
      DropSnapshotIfAny();
      ReleaseSRWLockExclusive(&g_lock);
      return false;
    }
    // Dormant/Mismatch/Unreadable: this creator didn't (correctly) write the
    // mapped slot — fall through to discovery attribution.
  }

  // 2) Discovery attribution (cold path — creator fires while a fresh edition
  //    is still discovering, e.g. the desktop is built on park entry after the
  //    gate opened). The snapshot diff shows exactly which slots THIS call
  //    wrote — proof they were written now, i.e. pre-layout. Rank those slots
  //    by xref ON DEMAND (the UI root dominates by reference count): a
  //    store-derived hook is authoritative for its own slot; a guard-tier hook
  //    (slotRva 0) ranks every slot it wrote. The dominant, xref-qualified
  //    slot is the canvas — accepted and scaled at the moment of creation.
  //    multiOffset is NOT a bar (the real canvas root carries several rects).
  Written written[4];
  const int wn = DiffSnapshot(bbW, bbH, written, 4);
  bool accepted = false;
  int pick = -1, bestX = -1, secondX = -1;
  for (int i = 0; i < wn && i < 4; ++i) {
    if (hookSlotRva && written[i].slotRva != hookSlotRva) continue;
    const int x = timing::SlotXrefCount(written[i].slotRva);
    if (x > bestX) {
      secondX = bestX;
      bestX = x;
      pick = i;
    } else if (x > secondX) {
      secondX = x;
    }
  }
  const bool qualifies = pick >= 0 && bestX >= GetConfig().minCanvasXref &&
                         secondX * 2 <= bestX;
  if (qualifies) {
    Cand* c = FindCandLocked(written[pick].slotRva);
    if (!c) {
      if (g_candCount >= kMaxCands) EvictWeakestLocked(/*allowProven=*/true);
      c = AddCandLocked(written[pick].slotRva, written[pick].rectOff,
                        written[pick].obj, false, written[pick].multiOffset);
    }
    if (c) {
      c->lastObj = written[pick].obj;
      c->rectOff = written[pick].rectOff;
      c->hasStore = true;
      c->storeChecked = true;
      c->xref = bestX;
      LOG("passive: creator wrote UI-root slot exe+0x%X rect+0x%X (%d code "
          "refs vs runner-up %d) — attribution + xref dominance",
          c->slotRva, c->rectOff, bestX, secondX < 0 ? 0 : secondX);
      AcceptLocked(*c, "creator wrote the UI-root slot (attribution + xref)");
      accepted = true;
    }
  } else if (pick >= 0) {
    LOG("passive: creator wrote slot exe+0x%X rect+0x%X (xref %d, runner-up "
        "%d) — not xref-dominant, not accepting",
        written[pick].slotRva, written[pick].rectOff, bestX,
        secondX < 0 ? 0 : secondX);
  } else if (wn == 0) {
    // The diff ran and the creator wrote NO backbuffer-rect slot: either the
    // canvas already exists (this is not its creation) or this hook is not a
    // canvas creator. Log the first few — this line existing at all proves
    // the hook fired (key diagnostic for editions that build the UI lazily).
    static int noWriteLogs = 0;
    if (noWriteLogs < 6) {
      ++noWriteLogs;
      LOG("passive: creator hook fired (slot ctx exe+0x%X) but wrote no "
          "backbuffer-rect slot this call — nothing to attribute",
          hookSlotRva);
    }
  }

  if (accepted && state::GetMapping(outSlotRva, outRectOff)) {
    ReleaseSRWLockExclusive(&g_lock);
    return true;
  }
  ReleaseSRWLockExclusive(&g_lock);
  return false;
}

}  // namespace passive
