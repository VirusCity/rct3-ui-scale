#include "selector.h"

#include <cstdint>

#include "cache/strategy_cache.h"
#include "core/config.h"
#include "core/log.h"
#include "core/patch.h"
#include "core/state.h"
#include "discovery/passive_discovery.h"
#include "hooks/timing_hook.h"

namespace selector {
namespace {

unsigned g_lastStableW = 0, g_lastStableH = 0;
bool g_cookieCleared = false;
bool g_armedThisSession = false;

}  // namespace

void Init(const char* dllDir) {
  cache::Init(dllDir);
  passive::Init();
  timing::Init();

  const Config& cfg = GetConfig();
  if (!cfg.enabled) {
    LOG("selector: [Scaling] Enabled=0 — staying inert (no hooks beyond the "
        "device gate, no discovery)");
    state::Set(State::Failed);
    return;
  }

  // Crash cookie: if the last session died while armed to scale, the cache
  // it trusted is suspect — discard it and rediscover from scratch.
  if (cfg.cacheEnabled && cache::ArmedCookieSet()) {
    LOG("selector: ARMED cookie found at attach — last session did not shut "
        "down cleanly after arming. Discarding cached mappings (self-heal).");
    cache::InvalidateStored();
    cache::WriteArmedCookie(false);
  }

  // Timing tier 1+2: guard-signature hooks need no data — arm them now,
  // before the game can create anything.
  timing::InstallGuardTierHooks();

  if (cfg.cacheEnabled) {
    // CONFIRMED mapping: publish (revalidated before every use) + arm its
    // store-derived hooks so the first creation this run scales pre-layout.
    cache::Entry e;
    if (cache::Load(&e)) {
      state::PublishMapping(e.globalSlotRva, e.canvasRectOffset);
      passive::SeedFromCache(e.globalSlotRva, e.canvasRectOffset);
      timing::InstallStoreHooksForSlot(e.globalSlotRva);
    } else {
      // PENDING candidates from a previous run. Score them by xref NOW (at
      // attach, off the hot path): the canvas root dominates by reference
      // count. If one clearly wins, promote it to a published+confirmed
      // mapping so the first creation this run scales pre-layout; otherwise
      // arm hooks on all and let attribution disambiguate at first creation.
      cache::Entry pend[8];
      const int n = cache::LoadPending(pend, 8);
      for (int i = 0; i < n; ++i)
        passive::SeedFromCache(pend[i].globalSlotRva, pend[i].canvasRectOffset);

      int best = -1, bestX = -1, secondX = -1;
      for (int i = 0; i < n; ++i) {
        const int x = timing::SlotXrefCount(pend[i].globalSlotRva);
        if (x > bestX) {
          secondX = bestX;
          bestX = x;
          best = i;
        } else if (x > secondX) {
          secondX = x;
        }
      }
      const bool dominant = best >= 0 && bestX >= cfg.minCanvasXref &&
                            secondX * 2 <= bestX;
      if (dominant) {
        state::PublishMapping(pend[best].globalSlotRva,
                              pend[best].canvasRectOffset);
        timing::InstallStoreHooksForSlot(pend[best].globalSlotRva);
        cache::StoreCatchSites(pend[best].globalSlotRva,
                               pend[best].canvasRectOffset);
        LOG("selector: promoted PENDING slot exe+0x%X rect+0x%X to CONFIRMED "
            "canvas by xref (%d refs vs runner-up %d) — will scale at first "
            "creation this run",
            pend[best].globalSlotRva, pend[best].canvasRectOffset, bestX,
            secondX < 0 ? 0 : secondX);
      } else {
        for (int i = 0; i < n; ++i)
          timing::InstallStoreHooksForSlot(pend[i].globalSlotRva);
        if (n)
          LOG("selector: %d pending candidate(s), no xref-dominant winner — "
              "armed all; attribution will confirm at first creation",
              n);
      }
    }
  }

  if (timing::InstalledCount() > 0 && cfg.apply) {
    g_armedThisSession = true;
    if (cfg.cacheEnabled) cache::WriteArmedCookie(true);
  }

  LOG("selector: init — state Discovering, %d creator hook(s) armed%s. "
      "Scaling requires a validated mapping AND a creator hook; otherwise "
      "this session only discovers + caches.",
      timing::InstalledCount(),
      timing::InstalledCount() ? "" : " (NONE — no scaling possible this run)");
}

void OnDetach() {
  // Clean shutdown: the cookie only needs to survive a crash.
  if (g_armedThisSession && GetConfig().cacheEnabled)
    cache::WriteArmedCookie(false);
}

void OnStable(IDirect3DDevice9* dev, unsigned bbW, unsigned bbH) {
  (void)dev;
  if (!GetConfig().enabled) return;
  if (g_lastStableW && (g_lastStableW != bbW || g_lastStableH != bbH)) {
    // Resolution change confirmed stable: reopen the revalidation window.
    passive::OnBackbufferChanged(bbW, bbH);
  } else if (state::Get() == State::Failed && !g_lastStableW) {
    state::Set(State::Discovering);
  }
  g_lastStableW = bbW;
  g_lastStableH = bbH;
  LOG("selector: backbuffer stable at %ux%u — discovery tick active", bbW,
      bbH);
}

void OnTick(IDirect3DDevice9* dev) {
  (void)dev;
  const Config& cfg = GetConfig();
  if (!cfg.enabled) return;

  passive::Tick(g_lastStableW, g_lastStableH);

  // The process survived past a successful pre-layout shrink: the cache that
  // produced it is proven — clear the crash cookie.
  if (!g_cookieCleared && state::ScaledSinceAttach()) {
    g_cookieCleared = true;
    if (cfg.cacheEnabled) cache::WriteArmedCookie(false);
    LOG("selector: first tick after a successful shrink — armed cookie "
        "cleared (cache proven)");
  }
}

void OnReset() {
  // The game rebuilds the canvas for the new device generation itself; our
  // patched values live in an object it rewrites (or frees). Forget patch
  // records rather than writing into a rebuilt/freed object; the creator
  // hook re-applies against the new backbuffer, validated fresh.
  LOG("selector: device reset — forgetting raw patches; timing hooks stay "
      "armed for the rebuild");
  patch::ForgetAll();
  state::Set(State::Discovering);
}

}  // namespace selector
