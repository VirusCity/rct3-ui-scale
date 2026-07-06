#include "backbuffer_gate.h"

#include "../core/config.h"
#include "../core/log.h"
#include "../core/patch.h"
#include "../core/state.h"
#include "../window/borderless.h"

namespace gate {
namespace {

// COM vtable slot indices, fixed by the interface declaration order in
// <d3d9.h> (the documented COM ABI — not a compiler-specific layout):
//   IDirect3D9::CreateDevice   = 16
//   IDirect3DDevice9::Reset    = 16
//   IDirect3DDevice9::Present  = 17
constexpr int kSlotCreateDevice = 16;
constexpr int kSlotReset = 16;
constexpr int kSlotPresent = 17;

using CreateDevice_t = HRESULT(__stdcall*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                           DWORD, D3DPRESENT_PARAMETERS*,
                                           IDirect3DDevice9**);
using Reset_t = HRESULT(__stdcall*)(IDirect3DDevice9*,
                                    D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*,
                                      const RECT*, HWND, const RGNDATA*);

CreateDevice_t g_origCreateDevice = nullptr;
Reset_t g_origReset = nullptr;
Present_t g_origPresent = nullptr;

bool g_d3dHooked = false;
bool g_deviceHooked = false;

StableCallback g_onStable = nullptr;
TickCallback g_onTick = nullptr;
ResetCallback g_onReset = nullptr;

// Stability tracking (render-thread only — D3D9 presents from one thread).
unsigned g_lastW = 0, g_lastH = 0;
int g_stableCount = 0;
bool g_stable = false;

bool IsPlaceholder(unsigned w, unsigned h) {
  return (w == 640 && h == 480) || (w == 1024 && h == 768);
}

void* VtblEntry(void* comObject, int slot) {
  return (*reinterpret_cast<void***>(comObject))[slot];
}

void TrackBackbuffer(IDirect3DDevice9* dev) {
  IDirect3DSurface9* bb = nullptr;
  if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb)
    return;
  D3DSURFACE_DESC desc = {};
  const HRESULT hr = bb->GetDesc(&desc);
  bb->Release();
  if (FAILED(hr)) return;

  state::SetBackbuffer(desc.Width, desc.Height);
  if (desc.Width == g_lastW && desc.Height == g_lastH) {
    ++g_stableCount;
  } else {
    if (g_lastW != 0)
      LOG("gate: backbuffer changed %ux%u -> %ux%u — stability reset",
          g_lastW, g_lastH, desc.Width, desc.Height);
    g_lastW = desc.Width;
    g_lastH = desc.Height;
    g_stableCount = 1;
    g_stable = false;
  }

  const Config& cfg = GetConfig();
  const int needed = IsPlaceholder(g_lastW, g_lastH)
                         ? cfg.stableFramesPlaceholder
                         : cfg.stableFrames;
  if (!g_stable && g_stableCount >= needed) {
    g_stable = true;
    LOG("gate: backbuffer STABLE at %ux%u after %d frames%s — gate open",
        g_lastW, g_lastH, g_stableCount,
        IsPlaceholder(g_lastW, g_lastH)
            ? " (placeholder-valued mode held long enough to be real)"
            : "");
    if (g_onStable) g_onStable(dev, g_lastW, g_lastH);
  }
}

HRESULT __stdcall PresentDetour(IDirect3DDevice9* dev, const RECT* src,
                                const RECT* dst, HWND wnd,
                                const RGNDATA* dirty) {
  TrackBackbuffer(dev);
  borderless::Tick();  // re-assert borderless window if the game restored chrome
  if (g_stable && g_onTick) g_onTick(dev);
  return g_origPresent(dev, src, dst, wnd, dirty);
}

HRESULT __stdcall ResetDetour(IDirect3DDevice9* dev,
                              D3DPRESENT_PARAMETERS* pp) {
  LOG("gate: device Reset requested (%ux%u) — closing gate, notifying "
      "restore",
      pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
  // Restore/invalidate BEFORE the reset so no scaled state survives into the
  // new device generation (idempotence: geometry scaled exactly once).
  g_stable = false;
  g_stableCount = 0;
  g_lastW = g_lastH = 0;
  if (g_onReset) g_onReset();
  borderless::PrepareParams(pp, nullptr);  // force target res + windowed
  const HRESULT hr = g_origReset(dev, pp);
  if (SUCCEEDED(hr)) {
    borderless::ApplyWindow(nullptr);
    // Publish the new dimensions NOW — the game rebuilds its UI (and runs
    // the creators) against the new device before the next Present.
    if (pp && pp->BackBufferWidth && pp->BackBufferHeight)
      state::SetBackbuffer(pp->BackBufferWidth, pp->BackBufferHeight);
  }
  return hr;
}

void HookDevice(IDirect3DDevice9* dev) {
  if (g_deviceHooked) return;
  void* resetEntry = VtblEntry(dev, kSlotReset);
  void* presentEntry = VtblEntry(dev, kSlotPresent);
  const bool ok =
      patch::Hook(resetEntry, reinterpret_cast<void*>(&ResetDetour),
                  reinterpret_cast<void**>(&g_origReset),
                  "IDirect3DDevice9::Reset") &&
      patch::Hook(presentEntry, reinterpret_cast<void*>(&PresentDetour),
                  reinterpret_cast<void**>(&g_origPresent),
                  "IDirect3DDevice9::Present");
  g_deviceHooked = ok;
  if (!ok)
    LOG("gate: device hook install failed — gate will never open (mod stays "
        "inert; game unaffected)");
}

HRESULT __stdcall CreateDeviceDetour(IDirect3D9* d3d, UINT adapter,
                                     D3DDEVTYPE type, HWND wnd, DWORD flags,
                                     D3DPRESENT_PARAMETERS* pp,
                                     IDirect3DDevice9** outDev) {
  // Rewrite present params BEFORE creation so the device is built windowed at
  // the target (monitor) resolution — this is how borderless renders above
  // the game's 1080p in-game cap.
  borderless::PrepareParams(pp, wnd);
  const HRESULT hr =
      g_origCreateDevice(d3d, adapter, type, wnd, flags, pp, outDev);
  LOG("gate: CreateDevice hr=0x%08lX backbuffer=%ux%u windowed=%d", hr,
      pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
      pp ? pp->Windowed : -1);
  if (SUCCEEDED(hr) && outDev && *outDev) {
    HookDevice(*outDev);
    borderless::ApplyWindow(wnd);
    // Publish dimensions BEFORE the game can run a creator on this device:
    // the timing hook validates freshly-written rects against these.
    IDirect3DSurface9* bb = nullptr;
    if (SUCCEEDED((*outDev)->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                                           &bb)) &&
        bb) {
      D3DSURFACE_DESC desc = {};
      if (SUCCEEDED(bb->GetDesc(&desc)))
        state::SetBackbuffer(desc.Width, desc.Height);
      bb->Release();
    }
  }
  return hr;
}

}  // namespace

void SetCallbacks(StableCallback onStable, TickCallback onTick,
                  ResetCallback onReset) {
  g_onStable = onStable;
  g_onTick = onTick;
  g_onReset = onReset;
}

void OnDirect3DCreated(IDirect3D9* d3d) {
  if (g_d3dHooked) return;
  void* entry = VtblEntry(d3d, kSlotCreateDevice);
  g_d3dHooked =
      patch::Hook(entry, reinterpret_cast<void*>(&CreateDeviceDetour),
                  reinterpret_cast<void**>(&g_origCreateDevice),
                  "IDirect3D9::CreateDevice");
}

bool IsStable() { return g_stable; }
unsigned Width() { return g_lastW; }
unsigned Height() { return g_lastH; }

}  // namespace gate
