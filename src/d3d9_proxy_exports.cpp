// d3d9_proxy_exports.cpp — verbatim export forwarders for the real d3d9.dll.
//
// These are the exports we DON'T wrap: D3DPERF_*, DebugSet*, the On12 creators,
// the shader validator, PSGP*, and the maximized-windowed shim. Each is a naked
// `jmp` stub to the real address — calling-convention agnostic, touching nothing
// on the stack so the real function returns straight to the game's caller.
//
// This file deliberately does NOT include <d3d9.h>: the header declares
// prototypes for several of these names (e.g. `int WINAPI D3DPERF_EndEvent()`),
// and a naked stub with a different signature would collide. By keeping the
// stub names out of the header's sight we can export them under their real
// names directly (see src/d3d9.def), with no aliasing.

#include <windows.h>

#include "logging.h"

#if !defined(_M_IX86)
#error "RCT3 is 32-bit. These naked stubs are x86-only. Build Win32."
#endif

// Every export forwarded verbatim. Names match src/d3d9.def exactly.
#define PASSTHROUGH_EXPORTS(X)                  \
  X(D3DPERF_BeginEvent)                         \
  X(D3DPERF_EndEvent)                           \
  X(D3DPERF_GetStatus)                          \
  X(D3DPERF_QueryRepeatFrame)                   \
  X(D3DPERF_SetMarker)                          \
  X(D3DPERF_SetOptions)                         \
  X(D3DPERF_SetRegion)                          \
  X(DebugSetLevel)                              \
  X(DebugSetMute)                               \
  X(Direct3D9EnableMaximizedWindowedModeShim)   \
  X(Direct3DCreate9On12)                        \
  X(Direct3DCreate9On12Ex)                      \
  X(Direct3DShaderValidatorCreate9)             \
  X(PSGPError)                                   \
  X(PSGPSampleTexture)

// One real-function-pointer slot per export (referenced by the asm stubs).
#define DECLARE_SLOT(name) void* g_real_##name = nullptr;
PASSTHROUGH_EXPORTS(DECLARE_SLOT)
#undef DECLARE_SLOT

// The naked jmp stubs, exported under their real names via the .def.
#define DEFINE_STUB(name)                                  \
  extern "C" __declspec(naked) void name() {               \
    __asm { jmp dword ptr [g_real_##name] }                \
  }
PASSTHROUGH_EXPORTS(DEFINE_STUB)
#undef DEFINE_STUB

// Called from proxy::Init() once the real d3d9.dll is loaded.
void ResolvePassthroughExports(HMODULE real) {
#define RESOLVE_SLOT(name)                                 \
  g_real_##name = (void*)GetProcAddress(real, #name);      \
  if (!g_real_##name) LOG("proxy: WARNING missing export %s", #name);
  PASSTHROUGH_EXPORTS(RESOLVE_SLOT)
#undef RESOLVE_SLOT
}
