#include "timing_hook.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../core/canvas_probe.h"
#include "../core/config.h"
#include "../core/log.h"
#include "../core/patch.h"
#include "../core/scale.h"
#include "../core/state.h"
#include "../discovery/passive_discovery.h"
#include "../sig/sigscan.h"

namespace timing {
namespace {

// Built-in signatures — every byte verified against the real binaries
// (RCT3.exe Complete + RCT3plus.exe Gold; see the sigcheck session log):
//   guard A unique @ 0x7B42F2 on Complete, 0 hits on Gold;
//   guard B unique @ 0x6278EE on Gold, 0 hits on Complete;
//   prologue A at both Complete creators; prologue B at the Gold creator;
//   A3-store walk-back derives exactly {0xA24A70, 0x7B42D0} / {0x6278E0}.
constexpr const char* kGuardA = "64 A3 00 00 00 00 83 3D ?? ?? ?? ?? 00 74";
constexpr const char* kPrologueA = "55 8B EC 6A FF 68";
constexpr size_t kGuardABack = 0x80;
constexpr const char* kGuardB =
    "A1 ?? ?? ?? ?? 64 89 25 00 00 00 00 83 EC ?? 85 C0 74";
constexpr const char* kPrologueB = "64 A1 00 00 00 00 6A FF 68";
constexpr size_t kGuardBBack = 0x40;
constexpr size_t kStoreBack = 0x300;  // store sits ~0x1A0 in on Complete

constexpr int kMaxHooks = 8;
constexpr int kMaxStoreSitesPerSlot = 4;
// A creator runs a handful of times per session. A hooked function that fires
// far more than that is NOT a creator (mis-derived hot function) — stop doing
// attribution work for it so it costs nearly nothing per call.
constexpr unsigned kMaxAttributionFires = 64;

using CreatorFn = void(__fastcall*)(void*, void*);

struct HookCtx {
  uintptr_t entryVA = 0;
  uint32_t slotRva = 0;  // slot this hook attributes to (0 = guard tier)
  CreatorFn orig = nullptr;
  bool live = false;
  unsigned fires = 0;
  bool quieted = false;  // logged the attribution-disabled notice
};

HookCtx g_hooks[kMaxHooks];
int g_hookCount = 0;
SRWLOCK g_installLock = SRWLOCK_INIT;

bool g_compiled = false;
sigscan::Pattern g_guardA, g_prologueA, g_guardB, g_prologueB;
bool g_guardAOk = false, g_guardBOk = false;

thread_local int t_depth = 0;

// ---------------------------------------------------------------------------
// The detour body. Timing only: the mapping is re-resolved from the global
// slot on every execution; validation gates every write; failure = no-op.
void OnCreator(int index, void* ecx, void* edx) {
  HookCtx& c = g_hooks[index];
  const bool outer = (t_depth == 0);
  // Full snapshot only when discovery needs the write-attribution diff: no
  // canvas mapping published yet, not Ready, and this hook hasn't proven it is
  // not a creator (fire cap). When a mapping IS published (cache/promoted/
  // xref-confirmed), step 1's pointer-change gate scales it without any
  // snapshot. PreCreator ALWAYS runs on the outer call to capture the mapping
  // slot's pre-pointer (the "created this call" proof).
  uint32_t pubS = 0, pubO = 0;
  const bool haveMapping = state::GetMapping(&pubS, &pubO);
  const bool wantSnapshot = outer && !haveMapping &&
                            state::Get() != State::Ready &&
                            c.fires < kMaxAttributionFires;
  if (outer) passive::PreCreator(wantSnapshot);
  ++t_depth;
  c.orig(ecx, edx);
  --t_depth;
  state::NoteCreatorRun();
  if (!outer) return;  // creators can nest (twin paths) — act once, outermost
  ++c.fires;
  // The first few fires of each hook are logged unconditionally: "did the
  // creator ever run" is the primary diagnostic on editions that build the
  // UI lazily (no fire line = the game never created a canvas this session).
  if (c.fires <= 4)
    LOG("timing: creator hook @ exe+0x%X FIRED (#%u, slot ctx exe+0x%X, "
        "state %d)",
        static_cast<unsigned>(c.entryVA - probe::ImageBase()), c.fires,
        c.slotRva, static_cast<int>(state::Get()));

  unsigned bbW = 0, bbH = 0;
  if (!state::GetBackbuffer(&bbW, &bbH)) {
    passive::DropSnapshot();
    return;  // device not up: do nothing
  }

  // Cheap lock-free pre-check: only enter the (locked) discovery path when
  // there is something to decide — a published mapping resolving to a FRESH
  // device-sized rect, or an open attribution window. Keeps a mis-derived
  // per-frame hook essentially free.
  bool proceed = wantSnapshot;
  uint32_t ms = 0, mo = 0;
  if (!proceed && state::GetMapping(&ms, &mo))
    proceed =
        probe::Resolve(ms, mo, bbW, bbH).status == probe::RectStatus::Unscaled;
  if (!proceed) {
    passive::DropSnapshot();
    if (c.fires == kMaxAttributionFires && !c.quieted) {
      c.quieted = true;
      LOG("timing: hook @ exe+0x%X fired %u times without an acceptance — "
          "not a creator; attribution disabled for it (validation path "
          "remains)",
          static_cast<unsigned>(c.entryVA - probe::ImageBase()), c.fires);
    }
    return;
  }

  uint32_t slot = 0, off = 0;
  if (!passive::OnCreatorFired(c.slotRva, bbW, bbH, &slot, &off)) return;

  // canvas = *(slot); rect = canvas + off — fresh, never cached.
  const probe::Resolved r = probe::Resolve(slot, off, bbW, bbH);
  if (r.status != probe::RectStatus::Unscaled) {
    if (r.status != probe::RectStatus::Scaled)
      LOG("timing: mapping exe+0x%X/+0x%X failed final validation (status %d) "
          "— not scaling",
          slot, off, static_cast<int>(r.status));
    return;
  }
  if (!GetConfig().apply) {
    state::Set(State::Ready);  // mapping validated; observe-only mode
    LOG("timing: mapping exe+0x%X/+0x%X validated at creation but Apply=0 — "
        "observing only",
        slot, off);
    return;
  }
  const float s = scale::Uniform(bbW, bbH);
  if (s <= 1.001f) {
    state::Set(State::Ready);  // validated; nothing to patch at ~1x
    return;
  }
  // Shrink the canvas to backbuffer/S so the engine's own device÷canvas
  // transform magnifies the whole UI — BEFORE anything lays out against it.
  const float w = r.r - r.l;
  const float h = r.b - r.t;
  float* rect = reinterpret_cast<float*>(r.rectVA);
  if (!patch::ApplyFloat(&rect[2], r.l + w / s, "canvas right (pre-layout)") ||
      !patch::ApplyFloat(&rect[3], r.t + h / s, "canvas bottom (pre-layout)")) {
    LOG("timing: canvas patch failed — leaving native");
    return;
  }
  state::Set(State::Ready);
  state::NoteScaled();
  LOG("timing: canvas %ux%u shrunk to %.0fx%.0f BEFORE layout (S=%.4f, slot "
      "exe+0x%X rect+0x%X) — UI lays out %.2fx larger, input scales natively",
      bbW, bbH, w / s, h / s, s, slot, off, s);
}

// Eight fixed detour thunks (MinHook needs a distinct target per hook and the
// detour needs to know which context fired).
#define DETOUR(n)                                            \
  void __fastcall Detour##n(void* ecx, void* edx) {          \
    OnCreator(n, ecx, edx);                                  \
  }
DETOUR(0) DETOUR(1) DETOUR(2) DETOUR(3)
DETOUR(4) DETOUR(5) DETOUR(6) DETOUR(7)
#undef DETOUR
void* const kDetours[kMaxHooks] = {
    reinterpret_cast<void*>(&Detour0), reinterpret_cast<void*>(&Detour1),
    reinterpret_cast<void*>(&Detour2), reinterpret_cast<void*>(&Detour3),
    reinterpret_cast<void*>(&Detour4), reinterpret_cast<void*>(&Detour5),
    reinterpret_cast<void*>(&Detour6), reinterpret_cast<void*>(&Detour7)};

// ---------------------------------------------------------------------------

// A real function entry is preceded by inter-function padding (verified on
// all three known creators: CC / C3 immediately before the entry).
bool LooksLikeEntry(const uint8_t* entry) {
  const uint8_t prev = entry[-1];
  if (prev == 0xCC || prev == 0xC3 || prev == 0x90) return true;
  return entry[-3] == 0xC2;  // ret imm16
}

// Nearest already-hooked entry in [from-maxBack, from], if any. Guards against
// the case where an EARLIER-installed hook (e.g. the guard tier) has already
// replaced a creator's prologue bytes with a MinHook JMP — the pattern
// walk-back would then skip past the real entry and land on the wrong
// function. A hooked entry in range is the true entry and must win.
uint8_t* NearestHookedEntry(const uint8_t* from, size_t maxBack) {
  const uintptr_t f = reinterpret_cast<uintptr_t>(from);
  const uintptr_t lo = f > maxBack ? f - maxBack : 0;
  uintptr_t best = 0;
  AcquireSRWLockShared(&g_installLock);
  for (int i = 0; i < g_hookCount; ++i) {
    const uintptr_t e = g_hooks[i].entryVA;
    if (e >= lo && e <= f && e > best) best = e;
  }
  ReleaseSRWLockShared(&g_installLock);
  return reinterpret_cast<uint8_t*>(best);
}

// Nearest padded prologue (either idiom) at or before `from`, preferring an
// already-hooked entry that sits between the pattern match and `from`.
uint8_t* WalkBackToEntry(const uint8_t* from, size_t maxBack) {
  uint8_t* a = g_compiled ? sigscan::FindBackward(from, maxBack, g_prologueA)
                          : nullptr;
  uint8_t* b = g_compiled ? sigscan::FindBackward(from, maxBack, g_prologueB)
                          : nullptr;
  uint8_t* best = nullptr;
  if (a && LooksLikeEntry(a)) best = a;
  if (b && LooksLikeEntry(b) && (!best || b > best)) best = b;
  // A hooked entry nearer to `from` than the pattern match is the real entry
  // (the pattern was defeated by that entry's own trampoline).
  if (uint8_t* h = NearestHookedEntry(from, maxBack))
    if (!best || h > best) best = h;
  return best;
}

// Install one hook (idempotent per entry). If the entry is already hooked at
// guard tier (slotRva 0) and a slot attribution arrives later, upgrade it.
bool AddHook(uintptr_t entryVA, uint32_t slotRva, const char* how) {
  AcquireSRWLockExclusive(&g_installLock);
  for (int i = 0; i < g_hookCount; ++i) {
    if (g_hooks[i].entryVA == entryVA) {
      if (slotRva && !g_hooks[i].slotRva) {
        g_hooks[i].slotRva = slotRva;
        LOG("timing: hook @ exe+0x%X now attributed to slot exe+0x%X (%s)",
            static_cast<unsigned>(entryVA - probe::ImageBase()), slotRva, how);
      }
      ReleaseSRWLockExclusive(&g_installLock);
      return true;
    }
  }
  if (g_hookCount >= kMaxHooks) {
    LOG("timing: hook table full (%d) — skipping entry exe+0x%X", kMaxHooks,
        static_cast<unsigned>(entryVA - probe::ImageBase()));
    ReleaseSRWLockExclusive(&g_installLock);
    return false;
  }
  const int i = g_hookCount;
  HookCtx& c = g_hooks[i];
  c.entryVA = entryVA;
  c.slotRva = slotRva;
  char name[64];
  sprintf_s(name, "creator exe+0x%X",
            static_cast<unsigned>(entryVA - probe::ImageBase()));
  if (!patch::Hook(reinterpret_cast<void*>(entryVA), kDetours[i],
                   reinterpret_cast<void**>(&c.orig), name)) {
    LOG("timing: hook install FAILED @ exe+0x%X (%s)",
        static_cast<unsigned>(entryVA - probe::ImageBase()), how);
    c = HookCtx{};
    ReleaseSRWLockExclusive(&g_installLock);
    return false;
  }
  c.live = true;
  ++g_hookCount;
  LOG("timing: creator hook %d live @ exe+0x%X (%s, slot exe+0x%X)", i,
      static_cast<unsigned>(entryVA - probe::ImageBase()), how, slotRva);
  ReleaseSRWLockExclusive(&g_installLock);
  return true;
}

// Compile an override or built-in pattern; returns whether `out` is usable.
bool CompileEffective(const char* iniValue, const char* builtin,
                      sigscan::Pattern& out, const char* what) {
  const char* text = (iniValue && iniValue[0]) ? iniValue : builtin;
  if (!sigscan::Compile(text, out)) {
    LOG("timing: %s signature failed to compile (%s) — tier disabled", what,
        (iniValue && iniValue[0]) ? "INI override" : "builtin");
    return false;
  }
  return true;
}

}  // namespace

bool Init() {
  const Config& cfg = GetConfig();
  g_guardAOk = CompileEffective(cfg.sigGuardA, kGuardA, g_guardA, "guard A");
  g_guardBOk = CompileEffective(cfg.sigGuardB, kGuardB, g_guardB, "guard B");
  const bool pA =
      CompileEffective(cfg.sigPrologueA, kPrologueA, g_prologueA, "prologue A");
  const bool pB =
      CompileEffective(cfg.sigPrologueB, kPrologueB, g_prologueB, "prologue B");
  g_compiled = pA && pB;
  return g_compiled;
}

// Twin-store expansion (defined after the store index below).
void ExpandTwinStores(uintptr_t entryVA, const char* tierName);

void InstallGuardTierHooks() {
  if (!g_compiled) return;
  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) return;

  struct GuardTier {
    bool ok;
    const sigscan::Pattern* guard;
    size_t maxBack;
    const char* name;
  } tiers[] = {
      {g_guardAOk, &g_guardA, kGuardABack, "guard A (Complete-style)"},
      {g_guardBOk, &g_guardB, kGuardBBack, "guard B (Gold-style)"},
  };
  for (const GuardTier& t : tiers) {
    if (!t.ok) continue;
    int n = 0;
    uint8_t* hit = sigscan::FindUnique(exe, *t.guard, &n);
    if (!hit) {
      LOG("timing: %s — %d match(es), need exactly 1; tier skipped", t.name, n);
      continue;
    }
    if (uint8_t* entry = WalkBackToEntry(hit, t.maxBack)) {
      AddHook(reinterpret_cast<uintptr_t>(entry), 0, t.name);
      // The guard may have found a dead twin — hook its running sibling(s).
      ExpandTwinStores(reinterpret_cast<uintptr_t>(entry), t.name);
    } else {
      LOG("timing: %s matched @ %p but no padded prologue within 0x%zX — "
          "tier skipped",
          t.name, static_cast<void*>(hit), t.maxBack);
    }
  }
  if (!g_hookCount)
    LOG("timing: no guard-tier hooks on this build — waiting for discovery "
        "to supply a slot for the store tier (scaling stays off until a "
        "creator hook exists)");
}

// --- store-target index ----------------------------------------------------
// The store tier needs, for a DISCOVERED slot, the code sites that write it —
// so it can hook their creator functions (data locating timing). Rather than
// re-scan .text per slot (thousands of candidates × MBs of code), scan the
// executable sections ONCE and index every `mov [abs], reg` whose target
// lands in writable data: A3 imm32 (eax) and 89 /r disp32 (ecx/edx/ebx/ebp/
// esi/edi). The immediate store C7 05 (the destructor's null write on both
// verified editions) is deliberately excluded. Sorted by target for O(log n)
// lookup.
struct StoreSite {
  uint32_t targetVA;
  uintptr_t siteVA;
};
std::vector<StoreSite> g_storeIndex;
bool g_indexBuilt = false;

void BuildStoreIndex() {
  if (g_indexBuilt) return;
  g_indexBuilt = true;
  HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) return;
  const auto base = reinterpret_cast<const uint8_t*>(exe);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);

  // Writable data extent, so we only index stores to globals.
  uint32_t dataLo = 0xFFFFFFFFu, dataHi = 0;
  const auto* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE) ||
        (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
      continue;
    const uint32_t lo =
        static_cast<uint32_t>(probe::ImageBase()) + sec->VirtualAddress;
    const DWORD sz = sec->Misc.VirtualSize ? sec->Misc.VirtualSize
                                           : sec->SizeOfRawData;
    dataLo = (lo < dataLo) ? lo : dataLo;
    if (lo + sz > dataHi) dataHi = lo + sz;
  }
  if (dataLo >= dataHi) return;

  auto inData = [&](uint32_t t) { return t >= dataLo && t < dataHi; };
  const uint8_t modrm[] = {0x0D, 0x15, 0x1D, 0x2D, 0x35, 0x3D};
  sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const uint8_t* s = base + sec->VirtualAddress;
    const DWORD sz = sec->Misc.VirtualSize ? sec->Misc.VirtualSize
                                           : sec->SizeOfRawData;
    for (DWORD p = 0; p + 6 <= sz; ++p) {
      uint32_t target;
      int len = 0;
      if (s[p] == 0xA3) {
        memcpy(&target, s + p + 1, 4);
        len = 1;
      } else if (s[p] == 0x89) {
        bool ok = false;
        for (uint8_t m : modrm)
          if (s[p + 1] == m) {
            ok = true;
            break;
          }
        if (!ok) continue;
        memcpy(&target, s + p + 2, 4);
        len = 2;
      } else {
        continue;
      }
      if (!inData(target)) continue;
      g_storeIndex.push_back(
          {target, reinterpret_cast<uintptr_t>(s + p)});
      (void)len;
    }
  }
  std::sort(g_storeIndex.begin(), g_storeIndex.end(),
            [](const StoreSite& a, const StoreSite& b) {
              return a.targetVA < b.targetVA;
            });
  LOG("timing: store index built — %zu global-store site(s) across .text",
      g_storeIndex.size());
}

// All store sites targeting slotVA (index into out up to maxOut). Returns the
// total count (may exceed maxOut).
int StoreSitesFor(uint32_t slotVA, uintptr_t* out, int maxOut) {
  BuildStoreIndex();
  auto lo = std::lower_bound(
      g_storeIndex.begin(), g_storeIndex.end(), slotVA,
      [](const StoreSite& s, uint32_t v) { return s.targetVA < v; });
  int n = 0;
  for (auto it = lo; it != g_storeIndex.end() && it->targetVA == slotVA; ++it) {
    if (n < maxOut) out[n] = it->siteVA;
    ++n;
  }
  return n;
}

int SlotStoreSiteCount(uint32_t slotRva) {
  if (!slotRva) return 0;
  uintptr_t sites[kMaxStoreSitesPerSlot];
  return StoreSitesFor(static_cast<uint32_t>(probe::ImageBase()) + slotRva,
                       sites, kMaxStoreSitesPerSlot);
}

// --- twin-store expansion ---------------------------------------------------
// A guard signature may land on a DEAD-TWIN creator (Complete's alt path never
// runs). The running twin contains a byte-identical store to the same global,
// so: take every absolute store inside the guard-located function's body and
// hook the OTHER code sites that store to the same target. The operand is
// used purely as an equality key between two code sites — it is never
// resolved, published, or trusted as data (the detour's snapshot-diff + xref
// attribution still makes every data decision). Verified offline: on Complete
// this derives exactly the main creator 0xA24A70 from the alt 0x7B42D0; on
// Gold the store has no twin and this is a no-op.
constexpr size_t kTwinBodyWindow = 0x300;
constexpr int kMaxTwinHooks = 4;

void ExpandTwinStores(uintptr_t entryVA, const char* tierName) {
  BuildStoreIndex();
  int installed = 0;
  for (const StoreSite& s : g_storeIndex) {
    if (s.siteVA < entryVA || s.siteVA >= entryVA + kTwinBodyWindow) continue;
    uintptr_t sites[kMaxStoreSitesPerSlot];
    const int n = StoreSitesFor(s.targetVA, sites, kMaxStoreSitesPerSlot);
    if (n < 2 || n > kMaxStoreSitesPerSlot) continue;  // no twin / not singleton
    for (int i = 0; i < n && installed < kMaxTwinHooks; ++i) {
      if (sites[i] >= entryVA && sites[i] < entryVA + kTwinBodyWindow)
        continue;  // the guard function's own store
      if (uint8_t* entry = WalkBackToEntry(
              reinterpret_cast<const uint8_t*>(sites[i]), kStoreBack)) {
        if (AddHook(reinterpret_cast<uintptr_t>(entry), 0, "twin-store"))
          ++installed;
      }
    }
  }
  if (installed)
    LOG("timing: twin-store expansion from %s @ exe+0x%X — %d twin creator "
        "hook(s) armed (first launch can now catch the real creator)",
        tierName, static_cast<unsigned>(entryVA - probe::ImageBase()),
        installed);
}

// Count of code references to a global (its VA appearing as a 4-byte operand
// anywhere in the executable sections). The canvas root is referenced by the
// whole UI (hundreds of sites); decoy device-rect globals by a handful — a
// large, static, watchpoint-free replacement for the old "read by many game
// functions" checklist. Results cached per slot (real globals are few).
std::vector<std::pair<uint32_t, int>> g_xrefCache;

int SlotXrefCount(uint32_t slotRva) {
  if (!slotRva) return 0;
  for (const auto& e : g_xrefCache)
    if (e.first == slotRva) return e.second;

  HMODULE exe = GetModuleHandleA(nullptr);
  int count = 0;
  if (exe) {
    const auto base = reinterpret_cast<const uint8_t*>(exe);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    uint8_t needle[4];
    const uint32_t va = static_cast<uint32_t>(probe::ImageBase()) + slotRva;
    memcpy(needle, &va, 4);
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
      if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
      const uint8_t* s = base + sec->VirtualAddress;
      const DWORD sz = sec->Misc.VirtualSize ? sec->Misc.VirtualSize
                                             : sec->SizeOfRawData;
      for (DWORD p = 0; p + 4 <= sz; ++p)
        if (s[p] == needle[0] && s[p + 1] == needle[1] &&
            s[p + 2] == needle[2] && s[p + 3] == needle[3])
          ++count;
    }
  }
  g_xrefCache.emplace_back(slotRva, count);
  return count;
}

void InstallStoreHooksForSlot(uint32_t slotRva) {
  if (!g_compiled || !slotRva) return;
  const uint32_t slotVA = static_cast<uint32_t>(probe::ImageBase()) + slotRva;
  uintptr_t sites[kMaxStoreSitesPerSlot];
  const int n = StoreSitesFor(slotVA, sites, kMaxStoreSitesPerSlot);
  if (n == 0) return;  // not a game-written global — nothing to hook (cheap)
  if (n > kMaxStoreSitesPerSlot) {
    LOG("timing: %d store sites for slot exe+0x%X — too many, refusing (not a "
        "singleton global)",
        n, slotRva);
    return;
  }
  int installed = 0;
  for (int i = 0; i < n; ++i) {
    if (uint8_t* entry = WalkBackToEntry(
            reinterpret_cast<const uint8_t*>(sites[i]), kStoreBack)) {
      if (AddHook(reinterpret_cast<uintptr_t>(entry), slotRva, "store-derived"))
        ++installed;
    } else {
      LOG("timing: store site %p for slot exe+0x%X has no padded prologue "
          "within 0x%zX — skipped",
          reinterpret_cast<void*>(sites[i]), slotRva, kStoreBack);
    }
  }
  if (installed)
    LOG("timing: armed %d store-derived creator hook(s) for slot exe+0x%X "
        "(%d store site(s))",
        installed, slotRva, n);
}

int InstalledCount() {
  return g_hookCount;
}

}  // namespace timing
