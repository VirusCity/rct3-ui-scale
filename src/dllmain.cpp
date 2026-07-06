// dllmain.cpp — entry point and one-time wiring (src-v2).
//
// Load order: Windows loads this d3d9.dll (next to the game exe) before the
// system one. On attach we ONLY: read config, open the log, init MinHook,
// load+resolve the real d3d9.dll, wire the selector to the backbuffer gate,
// and arm the TIMING hooks (signature-located creator hooks — timing only,
// they patch nothing by themselves).
//
// Crucially, no memory is scaled here. A shrink happens exclusively inside a
// creator detour, after passive discovery has validated a canvas mapping
// against a live backbuffer (correct hook + correct discovery, or nothing).

#include <windows.h>

#include <string>

#include "core/config.h"
#include "core/log.h"
#include "core/patch.h"
#include "device/backbuffer_gate.h"
#include "proxy.h"
#include "selector.h"

namespace {

std::string ModuleDir(HMODULE self) {
  char path[MAX_PATH];
  const DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
  std::string s(path, n);
  const size_t slash = s.find_last_of("\\/");
  return slash == std::string::npos ? std::string(".") : s.substr(0, slash);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  switch (reason) {
    case DLL_PROCESS_ATTACH: {
      DisableThreadLibraryCalls(inst);

      const std::string dir = ModuleDir(inst);
      LoadConfig((dir + "\\d3d9_uiscale.ini").c_str());
      const Config& cfg = GetConfig();

      logx::Init(cfg.logEnabled, (dir + "\\d3d9_uiscale.log").c_str());
      LOG("=====================================================");
      LOG("dllmain: attach (hybrid signature+data-flow build). dir=%s "
          "enabled=%d apply=%d override=%.3f reference=%dx%d",
          dir.c_str(), cfg.enabled, cfg.apply, cfg.scaleOverride,
          cfg.referenceWidth, cfg.referenceHeight);

      if (!proxy::Init()) {
        LOG("dllmain: proxy init failed — game may not render. Aborting.");
        return TRUE;  // stay loaded so exports still resolve as best-effort
      }

      if (!patch::Init())
        LOG("dllmain: MinHook init failed — no hooks will install; the mod "
            "stays inert");

      selector::Init(dir.c_str());
      gate::SetCallbacks(&selector::OnStable, &selector::OnTick,
                         &selector::OnReset);
      LOG("dllmain: wired. Timing hooks armed at attach; discovery waits for "
          "a stable backbuffer (startup-trap defense); scaling waits for "
          "hook + validated mapping.");
      break;
    }
    case DLL_PROCESS_DETACH: {
      LOG("dllmain: detach — restoring everything");
      selector::OnDetach();
      patch::Shutdown();  // RestoreAll + UnhookAll
      logx::Shutdown();
      break;
    }
  }
  return TRUE;
}
