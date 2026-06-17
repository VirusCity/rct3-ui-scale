#include "d3d9_hooks.h"

#include <windows.h>

#include <string>

#include "MinHook.h"
#include "frame_inspect.h"
#include "logging.h"
#include "ui_scale.h"

// The verbatim export forwarders live in d3d9_proxy_exports.cpp (a TU without
// <d3d9.h>, so their names don't collide with the header's prototypes).
void ResolvePassthroughExports(HMODULE real);

// ============================================================================
//  Proxy: load the real d3d9.dll and resolve the exports we wrap.
// ============================================================================
//
// At attach we LoadLibrary the genuine d3d9.dll from the system directory (WOW64
// redirects a 32-bit process to SysWOW64 automatically). Direct3DCreate9 and
// Direct3DCreate9Ex are *wrapped* (real functions, bottom of this file) so we
// can hook the device the game gets back; everything else is forwarded verbatim.

namespace {

HMODULE g_realDll = nullptr;

using PFN_Direct3DCreate9   = IDirect3D9* (WINAPI*)(UINT);
using PFN_Direct3DCreate9Ex = HRESULT     (WINAPI*)(UINT, IDirect3D9Ex**);

}  // namespace

// Real entry points for the two wrapped creators (external linkage so the
// wrapper definitions at the bottom of the file can see them).
PFN_Direct3DCreate9   g_real_Direct3DCreate9   = nullptr;
PFN_Direct3DCreate9Ex g_real_Direct3DCreate9Ex = nullptr;

namespace proxy {

bool Init() {
  if (g_realDll) return true;

  char sysdir[MAX_PATH];
  UINT n = GetSystemDirectoryA(sysdir, MAX_PATH);  // SysWOW64 under WOW64.
  if (n == 0 || n >= MAX_PATH) return false;

  std::string path = std::string(sysdir) + "\\d3d9.dll";
  g_realDll = LoadLibraryA(path.c_str());
  if (!g_realDll) {
    LOG("proxy: FAILED to load real d3d9.dll from %s (err %lu)", path.c_str(),
        GetLastError());
    return false;
  }
  LOG("proxy: loaded real d3d9.dll at %p (%s)", (void*)g_realDll, path.c_str());

  ResolvePassthroughExports(g_realDll);

  g_real_Direct3DCreate9 =
      (PFN_Direct3DCreate9)GetProcAddress(g_realDll, "Direct3DCreate9");
  g_real_Direct3DCreate9Ex =
      (PFN_Direct3DCreate9Ex)GetProcAddress(g_realDll, "Direct3DCreate9Ex");
  LOG("proxy: Direct3DCreate9=%p Direct3DCreate9Ex=%p",
      (void*)g_real_Direct3DCreate9, (void*)g_real_Direct3DCreate9Ex);

  return true;
}

void Shutdown() {
  if (g_realDll) {
    FreeLibrary(g_realDll);
    g_realDll = nullptr;
  }
}

}  // namespace proxy

// ============================================================================
//  Hooks: MinHook vtable hooks on IDirect3D9 / IDirect3DDevice9.
// ============================================================================
//
// vtable indices below come from the fixed COM method order of the public
// IDirect3DDevice9 / IDirect3D9 interfaces in d3d9.h — they are part of the
// stable D3D9 ABI, NOT build-specific offsets. We read each object's own vtable
// at runtime and hook by absolute function address (shared across instances),
// which is the MinHook-idiomatic approach.

namespace {

// IDirect3D9 vtable
constexpr int kIDirect3D9_CreateDevice = 16;

// IDirect3DDevice9 vtable
constexpr int kDev_Reset                 = 16;
constexpr int kDev_Present               = 17;
constexpr int kDev_EndScene              = 42;
constexpr int kDev_DrawPrimitive         = 81;
constexpr int kDev_DrawIndexedPrimitive  = 82;
constexpr int kDev_DrawPrimitiveUP       = 83;
constexpr int kDev_DrawIndexedPrimitiveUP = 84;
// Reserved for the scaling stage (gated on the frame-inspector findings):
// constexpr int kDev_SetTransform = 44;
// constexpr int kDev_SetViewport  = 47;

using PFN_CreateDevice = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                          DWORD, D3DPRESENT_PARAMETERS*,
                                          IDirect3DDevice9**);
using PFN_Present = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*,
                                     HWND, const RGNDATA*);
using PFN_EndScene = HRESULT(WINAPI*)(IDirect3DDevice9*);
using PFN_Reset = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PFN_DrawPrimitive =
    HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
using PFN_DrawIndexedPrimitive = HRESULT(WINAPI*)(
    IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using PFN_DrawPrimitiveUP = HRESULT(WINAPI*)(IDirect3DDevice9*,
                                             D3DPRIMITIVETYPE, UINT,
                                             const void*, UINT);
using PFN_DrawIndexedPrimitiveUP =
    HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT,
                     const void*, D3DFORMAT, const void*, UINT);

PFN_CreateDevice o_CreateDevice = nullptr;
PFN_Present      o_Present = nullptr;
PFN_EndScene     o_EndScene = nullptr;
PFN_Reset        o_Reset = nullptr;
PFN_DrawPrimitive             o_DrawPrimitive = nullptr;
PFN_DrawIndexedPrimitive      o_DrawIndexedPrimitive = nullptr;
PFN_DrawPrimitiveUP           o_DrawPrimitiveUP = nullptr;
PFN_DrawIndexedPrimitiveUP    o_DrawIndexedPrimitiveUP = nullptr;

bool g_d3d9Hooked = false;
bool g_deviceHooked = false;

// Helper: pull entry `index` out of a COM object's vtable.
void* VtblEntry(void* comObject, int index) {
  void** vtbl = *reinterpret_cast<void***>(comObject);
  return vtbl[index];
}

// ---- Device-method detours -------------------------------------------------
// Milestone 1: these are transparent — they only notify ui_scale (counters /
// logging) and forward. No geometry is altered yet. This lets us verify the
// whole proxy+hook chain renders identically to stock before any scaling.

HRESULT WINAPI Hook_Present(IDirect3DDevice9* dev, const RECT* src,
                            const RECT* dst, HWND wnd, const RGNDATA* dirty) {
  frameinspect::OnFrameBoundary(dev);
  uiscale::OnPresent(dev);
  return o_Present(dev, src, dst, wnd, dirty);
}

// ---- Draw-call detours -----------------------------------------------------
// Transparent: forward to the inspector (active only during a capture) then to
// the real method. This is also where the UI scale transform will hook in once
// the capture identifies the UI pass.

HRESULT WINAPI Hook_DrawPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type,
                                  UINT startVertex, UINT primCount) {
  frameinspect::OnDraw(dev, "DrawPrimitive", type, primCount);
  return o_DrawPrimitive(dev, type, startVertex, primCount);
}

HRESULT WINAPI Hook_DrawIndexedPrimitive(IDirect3DDevice9* dev,
                                         D3DPRIMITIVETYPE type, INT baseVertex,
                                         UINT minIndex, UINT numVertices,
                                         UINT startIndex, UINT primCount) {
  frameinspect::OnDraw(dev, "DrawIndexedPrimitive", type, primCount);
  return o_DrawIndexedPrimitive(dev, type, baseVertex, minIndex, numVertices,
                                startIndex, primCount);
}

HRESULT WINAPI Hook_DrawPrimitiveUP(IDirect3DDevice9* dev,
                                    D3DPRIMITIVETYPE type, UINT primCount,
                                    const void* data, UINT stride) {
  frameinspect::OnDraw(dev, "DrawPrimitiveUP", type, primCount);
  return o_DrawPrimitiveUP(dev, type, primCount, data, stride);
}

HRESULT WINAPI Hook_DrawIndexedPrimitiveUP(
    IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT minIndex,
    UINT numVertices, UINT primCount, const void* indexData,
    D3DFORMAT indexFormat, const void* vertexData, UINT stride) {
  frameinspect::OnDraw(dev, "DrawIndexedPrimitiveUP", type, primCount);
  return o_DrawIndexedPrimitiveUP(dev, type, minIndex, numVertices, primCount,
                                  indexData, indexFormat, vertexData, stride);
}

HRESULT WINAPI Hook_EndScene(IDirect3DDevice9* dev) {
  uiscale::OnEndScene(dev);
  return o_EndScene(dev);
}

HRESULT WINAPI Hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
  uiscale::OnPreReset(dev);
  HRESULT hr = o_Reset(dev, pp);
  uiscale::OnPostReset(dev, pp);
  return hr;
}

// ---- IDirect3D9::CreateDevice detour ---------------------------------------

HRESULT WINAPI Hook_CreateDevice(IDirect3D9* self, UINT adapter,
                                 D3DDEVTYPE type, HWND focus, DWORD flags,
                                 D3DPRESENT_PARAMETERS* pp,
                                 IDirect3DDevice9** ppDevice) {
  HRESULT hr = o_CreateDevice(self, adapter, type, focus, flags, pp, ppDevice);
  if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
    LOG("CreateDevice OK: %ux%u windowed=%d fmt=%d  device=%p",
        pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
        pp ? pp->Windowed : -1, pp ? pp->BackBufferFormat : 0,
        (void*)*ppDevice);
    hooks::InstallOnDevice(*ppDevice);
  } else {
    LOG("CreateDevice FAILED hr=0x%08lX", hr);
  }
  return hr;
}

}  // namespace

namespace hooks {

void InstallOnD3D9(IDirect3D9* d3d9) {
  if (g_d3d9Hooked || !d3d9) return;

  // MinHook is initialised once in dllmain; create+enable the CreateDevice hook.
  void* target = VtblEntry(d3d9, kIDirect3D9_CreateDevice);
  if (MH_CreateHook(target, &Hook_CreateDevice,
                    reinterpret_cast<void**>(&o_CreateDevice)) == MH_OK &&
      MH_EnableHook(target) == MH_OK) {
    g_d3d9Hooked = true;
    LOG("hooks: CreateDevice hooked (target=%p)", target);
  } else {
    LOG("hooks: FAILED to hook CreateDevice (target=%p)", target);
  }
}

void InstallOnDevice(IDirect3DDevice9* device) {
  if (g_deviceHooked || !device) return;

  struct Slot { int index; void* detour; void** orig; const char* name; };
  const Slot slots[] = {
      {kDev_Reset,    &Hook_Reset,    reinterpret_cast<void**>(&o_Reset),    "Reset"},
      {kDev_Present,  &Hook_Present,  reinterpret_cast<void**>(&o_Present),  "Present"},
      {kDev_EndScene, &Hook_EndScene, reinterpret_cast<void**>(&o_EndScene), "EndScene"},
      {kDev_DrawPrimitive, &Hook_DrawPrimitive,
       reinterpret_cast<void**>(&o_DrawPrimitive), "DrawPrimitive"},
      {kDev_DrawIndexedPrimitive, &Hook_DrawIndexedPrimitive,
       reinterpret_cast<void**>(&o_DrawIndexedPrimitive), "DrawIndexedPrimitive"},
      {kDev_DrawPrimitiveUP, &Hook_DrawPrimitiveUP,
       reinterpret_cast<void**>(&o_DrawPrimitiveUP), "DrawPrimitiveUP"},
      {kDev_DrawIndexedPrimitiveUP, &Hook_DrawIndexedPrimitiveUP,
       reinterpret_cast<void**>(&o_DrawIndexedPrimitiveUP),
       "DrawIndexedPrimitiveUP"},
  };

  bool allOk = true;
  for (const auto& s : slots) {
    void* target = VtblEntry(device, s.index);
    if (MH_CreateHook(target, s.detour, s.orig) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
      LOG("hooks: FAILED to hook device %s (target=%p)", s.name, target);
      allOk = false;
    } else {
      LOG("hooks: device %s hooked (target=%p)", s.name, target);
    }
  }
  g_deviceHooked = true;  // don't retry per-device; the targets are shared.

  uiscale::OnDeviceCreated(device);
  if (!allOk) LOG("hooks: some device hooks failed — see above");
}

}  // namespace hooks

// ============================================================================
//  Wrapped exports (defined here so they share the proxy state above).
// ============================================================================

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
  if (!g_real_Direct3DCreate9) {
    LOG("Direct3DCreate9: real entry missing!");
    return nullptr;
  }
  IDirect3D9* d3d9 = g_real_Direct3DCreate9(SDKVersion);
  LOG("Direct3DCreate9(SDK=%u) -> %p", SDKVersion, (void*)d3d9);
  if (d3d9) hooks::InstallOnD3D9(d3d9);
  return d3d9;
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion,
                                            IDirect3D9Ex** ppD3D) {
  if (!g_real_Direct3DCreate9Ex) {
    LOG("Direct3DCreate9Ex: real entry missing!");
    return E_NOTIMPL;
  }
  HRESULT hr = g_real_Direct3DCreate9Ex(SDKVersion, ppD3D);
  LOG("Direct3DCreate9Ex(SDK=%u) -> hr=0x%08lX d3d9ex=%p", SDKVersion, hr,
      ppD3D ? (void*)*ppD3D : nullptr);
  if (SUCCEEDED(hr) && ppD3D && *ppD3D)
    hooks::InstallOnD3D9(reinterpret_cast<IDirect3D9*>(*ppD3D));
  return hr;
}
