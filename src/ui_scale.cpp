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

void ScaleDrawIfUI(IDirect3DDevice9* dev, UINT firstVertex, UINT vertexCount) {
  const Config& cfg = GetConfig();
  if (!cfg.renderSideScale || cfg.scale == 1.0f || !dev || vertexCount == 0)
    return;

  // UI discriminator (proven by the frame inspector): fixed-function + XYZRHW.
  IDirect3DVertexShader9* vs = nullptr;
  dev->GetVertexShader(&vs);
  if (vs) { vs->Release(); return; }      // programmable => 3D world
  DWORD fvf = 0;
  dev->GetFVF(&fvf);
  if ((fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW) return;  // not pre-transformed

  IDirect3DVertexBuffer9* vb = nullptr;
  UINT offset = 0, stride = 0;
  if (FAILED(dev->GetStreamSource(0, &vb, &offset, &stride)) || !vb) return;
  if (stride < 16) { vb->Release(); return; }  // need at least x,y,z,rhw

  // Scale about the viewport center so centered windows stay put.
  D3DVIEWPORT9 vp{};
  dev->GetViewport(&vp);
  const float cx = vp.X + vp.Width * 0.5f;
  const float cy = vp.Y + vp.Height * 0.5f;
  const float s = cfg.scale;

  const UINT byteStart = offset + firstVertex * stride;
  const UINT byteLen = vertexCount * stride;

  void* p = nullptr;
  if (SUCCEEDED(vb->Lock(byteStart, byteLen, &p, 0)) && p) {
    float* v0 = reinterpret_cast<float*>(p);
    // Sanity check: real XYZRHW screen coords. If the VB is write-only the read
    // is garbage and this fails — then we skip (and log once) rather than corrupt.
    const float origX = v0[0], origY = v0[1];
    const bool sane = origX > -4000.f && origX < 16000.f &&
                      origY > -4000.f && origY < 16000.f;
    if (sane) {
      for (UINT i = 0; i < vertexCount; ++i) {
        float* v = reinterpret_cast<float*>(static_cast<char*>(p) + i * stride);
        v[0] = cx + (v[0] - cx) * s;
        v[1] = cy + (v[1] - cy) * s;
      }
      static bool loggedOk = false;
      if (!loggedOk) {
        loggedOk = true;
        LOG("uiscale: render-side scaling ACTIVE (scale=%.2f, center=%.0f,%.0f, "
            "stride=%u). First UI vert (%.1f,%.1f) -> (%.1f,%.1f).",
            s, cx, cy, stride, origX, origY, cx + (origX - cx) * s,
            cy + (origY - cy) * s);
      }
    } else {
      static bool loggedBad = false;
      if (!loggedBad) {
        loggedBad = true;
        LOG("uiscale: UI vertex buffer not readable (first vert %.1f,%.1f looks "
            "invalid) — render-side scaling skipped; VB is likely WRITEONLY.",
            v0[0], v0[1]);
      }
    }
    vb->Unlock();
  }
  vb->Release();
}

void OnPreReset(IDirect3DDevice9* /*device*/) {
  LOG("uiscale: pre-Reset (release D3DPOOL_DEFAULT resources here when added)");
}

void OnPostReset(IDirect3DDevice9* /*device*/, D3DPRESENT_PARAMETERS* pp) {
  LOG("uiscale: post-Reset %ux%u (recreate resources here when added)",
      pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
}

}  // namespace uiscale
