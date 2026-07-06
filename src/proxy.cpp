// proxy.cpp — real-DLL loading and the two wrapped creators.

#include "proxy.h"

#include <windows.h>

#include <cstdio>

#include <d3d9.h>

#include "core/log.h"
#include "device/backbuffer_gate.h"

void ResolvePassthroughExports(HMODULE real);  // proxy_exports.cpp

namespace proxy {
namespace {

using Create9_t = IDirect3D9*(WINAPI*)(UINT);
using Create9Ex_t = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);

Create9_t g_realCreate9 = nullptr;
Create9Ex_t g_realCreate9Ex = nullptr;

}  // namespace

bool Init() {
  char sysDir[MAX_PATH];
  if (!GetSystemDirectoryA(sysDir, MAX_PATH)) return false;

  char path[MAX_PATH];
  sprintf_s(path, "%s\\d3d9.dll", sysDir);
  HMODULE real = LoadLibraryA(path);
  if (!real) {
    LOG("proxy: failed to load %s (err %lu)", path, GetLastError());
    return false;
  }

  ResolvePassthroughExports(real);
  g_realCreate9 =
      reinterpret_cast<Create9_t>(GetProcAddress(real, "Direct3DCreate9"));
  g_realCreate9Ex =
      reinterpret_cast<Create9Ex_t>(GetProcAddress(real, "Direct3DCreate9Ex"));
  if (!g_realCreate9) {
    LOG("proxy: real Direct3DCreate9 missing — cannot continue");
    return false;
  }
  LOG("proxy: real d3d9 loaded from %s", path);
  return true;
}

}  // namespace proxy

// ---- wrapped exports (named in src-v2/d3d9.def) ----------------------------

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion) {
  if (!proxy::g_realCreate9) return nullptr;
  IDirect3D9* d3d = proxy::g_realCreate9(sdkVersion);
  LOG("proxy: Direct3DCreate9(sdk=%u) -> %p", sdkVersion, d3d);
  if (d3d) gate::OnDirect3DCreated(d3d);
  return d3d;
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdkVersion,
                                            IDirect3D9Ex** out) {
  if (!proxy::g_realCreate9Ex) return E_FAIL;
  const HRESULT hr = proxy::g_realCreate9Ex(sdkVersion, out);
  LOG("proxy: Direct3DCreate9Ex(sdk=%u) -> hr=0x%08lX", sdkVersion, hr);
  // IDirect3D9Ex derives from IDirect3D9; CreateDevice sits at the same slot.
  if (SUCCEEDED(hr) && out && *out)
    gate::OnDirect3DCreated(reinterpret_cast<IDirect3D9*>(*out));
  return hr;
}
