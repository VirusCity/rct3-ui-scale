#include "ui_scale.h"

#include "config.h"
#include "logging.h"

namespace uiscale {
namespace {

unsigned long long g_frame = 0;

// Runtime on/off (toggled by the hotkey); initial state from [Scaling] Enabled.
bool g_runtimeEnabled = true;
bool g_togglePrevDown = false;

// Diagnostic: when > 0, log the next N scaled UI draws' bbox + anchor (armed by
// the frame-inspector capture key) so we can see how each element is anchored.
int g_dumpRects = 0;

// Per-frame record of each scaled UI element: its final on-screen rect plus the
// anchor/scale used, so the input remap can (a) tell when the cursor is over a
// UI element and (b) invert exactly that element's transform. Double-buffered:
// the renderer fills `build` during a frame; at Present we swap it to `active`,
// which the window-proc reads. Cursors NOT over any rect pass through unchanged
// (world picking/placement stays 1:1).
struct UiRect {
  float x0, y0, x1, y1;  // scaled screen rect
  float ax, ay;          // anchor used
  float s;               // scale used
};
constexpr int kMaxRects = 2048;
UiRect g_bufA[kMaxRects];
UiRect g_bufB[kMaxRects];
UiRect* g_build = g_bufA;
UiRect* g_active = g_bufB;
int g_buildN = 0;
int g_activeN = 0;
CRITICAL_SECTION g_cs;
bool g_csInit = false;

void RecordRect(float x0, float y0, float x1, float y1, float ax, float ay,
                float s) {
  if (g_buildN < kMaxRects)
    g_build[g_buildN++] = UiRect{x0, y0, x1, y1, ax, ay, s};
}

}  // namespace

void OnDeviceCreated(IDirect3DDevice9* device) {
  LOG("uiscale: device created %p, configured scale=%.3f", (void*)device,
      GetConfig().scale);
  g_frame = 0;
  g_runtimeEnabled = GetConfig().renderSideScale;
  if (!g_csInit) {
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
  }
  g_buildN = 0;
  g_activeN = 0;
}

// Arm a one-shot dump of the next `n` scaled UI draws' bbox + anchor (diagnostic).
void RequestRectDump(int n) { g_dumpRects = n; }

// Map a window-message cursor (x,y) to the original layout coordinate IF it sits
// over a scaled UI element (searched top-most first). Returns false otherwise,
// so the caller leaves world/3D cursors untouched.
bool MapCursorToOriginal(int x, int y, int& ox, int& oy) {
  if (!g_csInit) return false;
  bool hit = false;
  EnterCriticalSection(&g_cs);
  for (int i = g_activeN - 1; i >= 0; --i) {
    const UiRect& r = g_active[i];
    if (x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) {
      ox = static_cast<int>(r.ax + (x - r.ax) / r.s + 0.5f);
      oy = static_cast<int>(r.ay + (y - r.ay) / r.s + 0.5f);
      hit = true;
      break;
    }
  }
  LeaveCriticalSection(&g_cs);
  return hit;
}

void OnPresent(IDirect3DDevice9* /*device*/) {
  ++g_frame;

  // Runtime scale toggle (e.g. off on menus, on in a park).
  const unsigned tk = GetConfig().toggleKey;
  const bool tdown = tk && (GetAsyncKeyState(static_cast<int>(tk)) & 0x8000) != 0;
  if (tdown && !g_togglePrevDown) {
    g_runtimeEnabled = !g_runtimeEnabled;
    LOG("uiscale: scaling toggled %s (key 0x%X)", g_runtimeEnabled ? "ON" : "OFF",
        tk);
  }
  g_togglePrevDown = tdown;

  // End of frame: publish the UI rects we collected to the input remap.
  if (g_csInit) {
    EnterCriticalSection(&g_cs);
    UiRect* tmp = g_active;
    g_active = g_build;
    g_build = tmp;
    g_activeN = g_buildN;
    g_buildN = 0;
    LeaveCriticalSection(&g_cs);
  }

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

// Choose the scaling anchor by EDGE CONTACT, not by where the center falls. Only
// elements that actually touch a screen edge (within a margin) dock to that
// edge; everything else (floating windows, mid-screen panels, icon columns)
// anchors to screen center. This keeps a window's body and its chrome — and a
// stack of icons — sharing one anchor, so they scale together instead of being
// split across zone boundaries and flying apart.
void EdgeAnchor(float minx, float miny, float maxx, float maxy, float left,
                float top, float W, float H, float& ax, float& ay) {
  const float mx = W * 0.06f, my = H * 0.06f;
  const float cx = left + W * 0.5f, cy = top + H * 0.5f;
  const float bcx = (minx + maxx) * 0.5f, bcy = (miny + maxy) * 0.5f;

  // "Docked" = touches an edge AND is a thin strip along it. The thinness test
  // (extends <30% of the perpendicular dimension from the edge) stops large
  // elements that merely REACH an edge — e.g. a window's tall chrome that
  // touches the top — from being yanked to the corner; those stay floating.
  const bool dl = (minx <= left + mx)     && (maxx <= left + 0.30f * W);
  const bool dr = (maxx >= left + W - mx) && (minx >= left + 0.70f * W);
  const bool dt = (miny <= top + my)      && (maxy <= top + 0.30f * H);
  const bool db = (maxy >= top + H - my)  && (miny >= top + 0.70f * H);

  // Top/bottom strips place their free (X) axis by thirds: the wide, centered
  // top menu bar lands in the middle third -> center anchor, so it isn't split
  // between two opposite corners.
  auto third = [](float c, float lo, float span) {
    return (c < lo + span / 3)       ? lo
           : (c > lo + 2 * span / 3) ? lo + span
                                     : lo + span * 0.5f;
  };

  // Left/right strips (the side toolbars) place their free (Y) axis by the HALF
  // they're in, so a column that lives entirely in one half shares ONE anchor
  // and scales uniformly. Thirds here would split a column crossing the 2/3 line
  // (upper icons -> center, lower -> bottom), piling the icons up.
  ax = dl ? left : dr ? (left + W) : (dt || db) ? third(bcx, left, W) : cx;
  ay = dt ? top : db ? (top + H) : (dl || dr) ? (bcy < cy ? top : top + H) : cy;
}

void ScaleDrawIfUI(IDirect3DDevice9* dev, UINT firstVertex, UINT vertexCount) {
  const Config& cfg = GetConfig();
  // g_runtimeEnabled folds in [Scaling] Enabled and the runtime toggle hotkey.
  if (!g_runtimeEnabled || cfg.scale == 1.0f || !dev || vertexCount == 0)
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

  D3DVIEWPORT9 vp{};
  dev->GetViewport(&vp);
  const float left = static_cast<float>(vp.X);
  const float top = static_cast<float>(vp.Y);
  const float W = static_cast<float>(vp.Width);
  const float H = static_cast<float>(vp.Height);
  const float s = cfg.scale;

  void* p = nullptr;
  if (FAILED(vb->Lock(offset + firstVertex * stride, vertexCount * stride, &p, 0)) ||
      !p) {
    vb->Release();
    return;
  }

  auto vert = [&](UINT i) {
    return reinterpret_cast<float*>(static_cast<char*>(p) + i * stride);
  };

  // Reject unreadable (write-only) buffers: the first vertex must be a plausible
  // screen coordinate, else we'd be scaling garbage.
  float* f0 = vert(0);
  if (!(f0[0] > -8000.f && f0[0] < 24000.f && f0[1] > -8000.f && f0[1] < 24000.f)) {
    static bool loggedBad = false;
    if (!loggedBad) {
      loggedBad = true;
      LOG("uiscale: UI vertex buffer not readable (first vert %.1f,%.1f) — "
          "scaling skipped; VB is likely WRITEONLY.", f0[0], f0[1]);
    }
    vb->Unlock();
    vb->Release();
    return;
  }

  // Bounding box of this draw's vertices.
  float minx = f0[0], maxx = f0[0], miny = f0[1], maxy = f0[1];
  for (UINT i = 1; i < vertexCount; ++i) {
    float* v = vert(i);
    if (v[0] < minx) minx = v[0]; else if (v[0] > maxx) maxx = v[0];
    if (v[1] < miny) miny = v[1]; else if (v[1] > maxy) maxy = v[1];
  }

  // Leave near-fullscreen quads (backgrounds / dimmers) alone — scaling them only
  // pushes their edges off-screen.
  if ((maxx - minx) > 0.85f * W && (maxy - miny) > 0.85f * H) {
    vb->Unlock();
    vb->Release();
    return;
  }

  // Anchor by edge contact (see EdgeAnchor): docked elements stay on their edge;
  // floating windows + their chrome share the center anchor and move together.
  float ax, ay;
  EdgeAnchor(minx, miny, maxx, maxy, left, top, W, H, ax, ay);

  for (UINT i = 0; i < vertexCount; ++i) {
    float* v = vert(i);
    v[0] = ax + (v[0] - ax) * s;
    v[1] = ay + (v[1] - ay) * s;
  }

  // Record the scaled rect so the input remap can hit-test the cursor against it.
  RecordRect(ax + (minx - ax) * s, ay + (miny - ay) * s, ax + (maxx - ax) * s,
             ay + (maxy - ay) * s, ax, ay, s);

  if (g_dumpRects > 0) {
    --g_dumpRects;
    LOG("uiscale[dump]: %s bbox [%.0f,%.0f .. %.0f,%.0f] (%.0fx%.0f) "
        "anchor (%.0f,%.0f) stride=%u",
        ((minx <= left + W * 0.06f || maxx >= left + W - W * 0.06f ||
          miny <= top + H * 0.06f || maxy >= top + H - H * 0.06f)
             ? "DOCKED"
             : "float "),
        minx, miny, maxx, maxy, maxx - minx, maxy - miny, ax, ay, stride);
  }

  static bool loggedOk = false;
  if (!loggedOk) {
    loggedOk = true;
    LOG("uiscale: scaling ACTIVE (scale=%.2f, viewport %.0fx%.0f). First UI draw "
        "bbox [%.0f,%.0f .. %.0f,%.0f] anchored at (%.0f,%.0f).",
        s, W, H, minx, miny, maxx, maxy, ax, ay);
  }

  vb->Unlock();
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
