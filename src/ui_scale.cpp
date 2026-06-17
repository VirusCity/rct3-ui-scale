#include "ui_scale.h"

#include "config.h"
#include "logging.h"

namespace uiscale {
namespace {

unsigned long long g_frame = 0;

}  // namespace

void OnDeviceCreated(IDirect3DDevice9* device) {
  LOG("uiscale: device created %p, configured scale=%.3f", (void*)device,
      GetConfig().scale);
  g_frame = 0;
}

void OnPresent(IDirect3DDevice9* /*device*/) {
  ++g_frame;
  // Heartbeat so the log shows the hook chain is alive without flooding.
  if (g_frame == 1 || g_frame % 600 == 0)
    LOG("uiscale: frame %llu (Present hook live)", g_frame);
}

void OnEndScene(IDirect3DDevice9* /*device*/) {
  // ========================================================================
  // TODO(scaling) — gated on research/renderdoc_findings.md
  // ------------------------------------------------------------------------
  // This is where UI rescaling will be implemented once the user captures the
  // UI render pass in RenderDoc and we know how to distinguish it from the 3D
  // world. The intended sequence (per CLAUDE.md "Known hard problems"):
  //
  //   1. Detect the UI pass. Candidate heuristic to CONFIRM against captures:
  //      ZENABLE disabled + ALPHABLENDENABLE on + orthographic/identity
  //      transform. Do not assume — verify before relying on it.
  //   2. Scale UI quads about the correct screen anchor (edge/corner aware),
  //      not the origin, so elements don't drift off-screen.
  //   3. Remap mouse/hit-testing coordinates so clicks land on the scaled UI.
  //
  // Whether this lives at EndScene, in per-draw-call hooks
  // (DrawIndexedPrimitive/SetTransform), or as a static in-memory patch is the
  // decision we make from the captures. Until then this is intentionally inert.
  // ========================================================================
}

void OnPreReset(IDirect3DDevice9* /*device*/) {
  LOG("uiscale: pre-Reset (release D3DPOOL_DEFAULT resources here when added)");
}

void OnPostReset(IDirect3DDevice9* /*device*/, D3DPRESENT_PARAMETERS* pp) {
  LOG("uiscale: post-Reset %ux%u (recreate resources here when added)",
      pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
}

}  // namespace uiscale
