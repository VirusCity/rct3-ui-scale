#include "frame_inspect.h"

#include <windows.h>

#include "config.h"
#include "logging.h"

namespace frameinspect {
namespace {

bool g_prevKeyDown = false;  // hotkey edge detection
bool g_armed = false;        // capture requested for the next frame
bool g_capturing = false;    // currently inside a captured frame
unsigned g_drawIndex = 0;
unsigned long long g_frame = 0;
int g_uiBoundaryDraw = -1;   // first draw that looks like UI (Z off / RHW)

// Cached discriminating state, to flag the world->UI transition.
struct KeyState {
  DWORD zenable = 0xFFFFFFFF;
  DWORD alphablend = 0xFFFFFFFF;
  bool  hasVS = false;
  bool  valid = false;
};
KeyState g_prev;

// Decode the interesting FVF bits (esp. XYZRHW — pre-transformed screen coords,
// which would mean the UI bypasses the transform pipeline entirely).
void DescribeFVF(DWORD fvf, char* out, size_t n) {
  if (fvf == 0) {
    _snprintf_s(out, n, _TRUNCATE, "0 (decl/shader)");
    return;
  }
  char flags[160] = {0};
  auto add = [&](const char* s) {
    strncat_s(flags, sizeof(flags), s, _TRUNCATE);
  };
  if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) add("XYZRHW ");
  else if (fvf & D3DFVF_XYZ) add("XYZ ");
  if (fvf & D3DFVF_NORMAL)   add("NORMAL ");
  if (fvf & D3DFVF_DIFFUSE)  add("DIFFUSE ");
  if (fvf & D3DFVF_SPECULAR) add("SPECULAR ");
  int tex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  char texbuf[16];
  _snprintf_s(texbuf, sizeof(texbuf), _TRUNCATE, "TEX%d", tex);
  add(texbuf);
  _snprintf_s(out, n, _TRUNCATE, "0x%lX (%s)", fvf, flags);
}

const char* PrimName(D3DPRIMITIVETYPE t) {
  switch (t) {
    case D3DPT_POINTLIST:     return "POINTLIST";
    case D3DPT_LINELIST:      return "LINELIST";
    case D3DPT_LINESTRIP:     return "LINESTRIP";
    case D3DPT_TRIANGLELIST:  return "TRILIST";
    case D3DPT_TRIANGLESTRIP: return "TRISTRIP";
    case D3DPT_TRIANGLEFAN:   return "TRIFAN";
    default:                  return "?";
  }
}

void StartCapture(IDirect3DDevice9* dev) {
  g_capturing = true;
  g_drawIndex = 0;
  g_uiBoundaryDraw = -1;
  g_prev = KeyState{};

  D3DVIEWPORT9 vp{};
  if (dev) dev->GetViewport(&vp);
  LOG("=== FRAME CAPTURE START (frame %llu) ===", g_frame);
  LOG("  viewport X=%lu Y=%lu W=%lu H=%lu minZ=%.2f maxZ=%.2f", vp.X, vp.Y,
      vp.Width, vp.Height, vp.MinZ, vp.MaxZ);
  LOG("  legend: Z=ZENABLE Zw=ZWRITE A=ALPHABLEND cull=CULLMODE "
      "L=LIGHTING VS=vertexshader");
}

void FinishCapture() {
  LOG("=== FRAME CAPTURE END: %u draws; UI boundary @ draw %d ===", g_drawIndex,
      g_uiBoundaryDraw);
  g_capturing = false;
}

}  // namespace

void OnFrameBoundary(IDirect3DDevice9* device) {
  ++g_frame;
  const Config& cfg = GetConfig();
  if (!cfg.diagnostics) return;

  // Finish a frame that was being captured (its draws are now all in the log).
  if (g_capturing) FinishCapture();

  // Begin capturing if a request is pending from last frame.
  if (g_armed) {
    g_armed = false;
    StartCapture(device);
  }

  // Hotkey rising edge -> arm capture for the *next* frame.
  bool down = (GetAsyncKeyState(static_cast<int>(cfg.captureKey)) & 0x8000) != 0;
  bool edge = down && !g_prevKeyDown;
  g_prevKeyDown = down;
  if (edge && !g_capturing) {
    g_armed = true;
    LOG("frameinspect: capture armed (key 0x%X) — dumping next frame",
        cfg.captureKey);
  }
}

void OnDraw(IDirect3DDevice9* dev, const char* call, D3DPRIMITIVETYPE type,
            UINT primitiveCount) {
  if (!g_capturing || !dev) return;

  DWORD zenable = 0, zwrite = 0, alpha = 0, cull = 0, lighting = 0;
  dev->GetRenderState(D3DRS_ZENABLE, &zenable);
  dev->GetRenderState(D3DRS_ZWRITEENABLE, &zwrite);
  dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &alpha);
  dev->GetRenderState(D3DRS_CULLMODE, &cull);
  dev->GetRenderState(D3DRS_LIGHTING, &lighting);

  DWORD fvf = 0;
  dev->GetFVF(&fvf);
  IDirect3DVertexShader9* vs = nullptr;
  dev->GetVertexShader(&vs);
  bool hasVS = vs != nullptr;
  if (vs) vs->Release();

  char fvfDesc[200];
  DescribeFVF(fvf, fvfDesc, sizeof(fvfDesc));

  // Flag the world->UI transition the first time depth testing turns off, a
  // pre-transformed FVF appears, or the pipeline drops to fixed-function.
  bool looksUI = (zenable == D3DZB_FALSE) ||
                 ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW);
  if (g_prev.valid &&
      (g_prev.zenable != zenable || g_prev.alphablend != alpha ||
       g_prev.hasVS != hasVS)) {
    LOG("  -- STATE CHANGE @ draw %u: Z %lu->%lu  A %lu->%lu  VS %d->%d --",
        g_drawIndex, g_prev.zenable, zenable, g_prev.alphablend, alpha,
        g_prev.hasVS ? 1 : 0, hasVS ? 1 : 0);
  }
  if (g_uiBoundaryDraw < 0 && looksUI) {
    g_uiBoundaryDraw = static_cast<int>(g_drawIndex);
    LOG("  >> likely UI pass begins at draw %u <<", g_drawIndex);
    D3DMATRIX proj{};
    if (SUCCEEDED(dev->GetTransform(D3DTS_PROJECTION, &proj)))
      LOG("     PROJECTION row0 [%.3f %.3f %.3f %.3f] row3 [%.3f %.3f %.3f %.3f]",
          proj.m[0][0], proj.m[0][1], proj.m[0][2], proj.m[0][3], proj.m[3][0],
          proj.m[3][1], proj.m[3][2], proj.m[3][3]);
  }

  LOG("  [draw %3u] %-22s %-8s prims=%-5u | Z=%lu Zw=%lu A=%lu cull=%lu L=%lu "
      "VS=%d FVF=%s",
      g_drawIndex, call, PrimName(type), primitiveCount, zenable, zwrite, alpha,
      cull, lighting, hasVS ? 1 : 0, fvfDesc);

  g_prev = KeyState{zenable, alpha, hasVS, true};
  ++g_drawIndex;
}

}  // namespace frameinspect
