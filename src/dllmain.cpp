// dllmain.cpp — entry point and one-time wiring.
//
// Load order: Windows loads this d3d9.dll (sitting next to the game exe) before
// the system one. On attach we: read config, open the log, load+resolve the
// real d3d9.dll, and initialise MinHook. The actual device hooks are installed
// lazily when the game calls our wrapped Direct3DCreate9 (see d3d9_hooks.cpp).

#include <windows.h>

#include <string>

#include "MinHook.h"
#include "config.h"
#include "d3d9_hooks.h"
#include "input_remap.h"
#include "logging.h"
#include "source_patch.h"

namespace {

// Directory containing this DLL (i.e. the game folder), for locating the .ini
// and log file. Returns with a trailing backslash stripped.
std::string ModuleDir(HMODULE self) {
  char path[MAX_PATH];
  DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
  std::string s(path, n);
  size_t slash = s.find_last_of("\\/");
  return slash == std::string::npos ? std::string(".") : s.substr(0, slash);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID /*reserved*/) {
  switch (reason) {
    case DLL_PROCESS_ATTACH: {
      DisableThreadLibraryCalls(inst);

      const std::string dir = ModuleDir(inst);
      LoadConfig(dir + "\\d3d9_uiscale.ini");

      const Config& cfg = GetConfig();
      const std::string logPath =
          cfg.logPath.empty() ? dir + "\\d3d9_uiscale.log" : cfg.logPath;
      logger::Init(cfg.logEnabled, logPath);
      LOG("dllmain: attach. dir=%s scale=%.3f logging=%d", dir.c_str(),
          cfg.scale, cfg.logEnabled ? 1 : 0);

      // Experimental source patch: raise the GUI2 per-element default scale at
      // its source. Must run before the game constructs any UI element, so we do
      // it here at attach (the exe is mapped; no UI exists yet). Independent of
      // the D3D proxy/hooks below.
      if (cfg.sourcePatch) {
        if (cfg.renderSideScale)
          LOG("dllmain: WARNING — both SourcePatch and render-side Scaling are "
              "on; the UI will be scaled twice. Disable one.");
        sourcepatch::ApplyGui2ScaleDefault(cfg.scale);
      }

      if (!proxy::Init()) {
        LOG("dllmain: proxy init failed — the game may not render. Aborting hook setup.");
        return TRUE;  // stay loaded so exports still forward where possible.
      }

      if (MH_Initialize() != MH_OK)
        LOG("dllmain: MH_Initialize failed — device hooks will not install.");
      else
        LOG("dllmain: MinHook initialised. Awaiting Direct3DCreate9.");
      break;
    }

    case DLL_PROCESS_DETACH: {
      LOG("dllmain: detach.");
      inputremap::Remove();
      MH_DisableHook(MH_ALL_HOOKS);
      MH_Uninitialize();
      logger::Shutdown();
      proxy::Shutdown();
      break;
    }
  }
  return TRUE;
}
