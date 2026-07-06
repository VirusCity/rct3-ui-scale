// selector.h — state-driven execution model (hybrid signature + data-flow).
//
//   enum class State { Discovering, Ready, Failed };   // core/state.h
//
//   attach:   cookie check -> load cache -> arm timing hooks (guard tier +
//             store tier from cached slots). NOTHING is patched here.
//   Present:  update backbuffer; when stable, run a passive discovery /
//             revalidation pass (persistent tick loop — independent of the
//             timing hooks).
//   creator:  timing hook fires -> attribute/validate -> shrink the fresh
//             canvas rect pre-layout -> Ready.
//
// Core principle: correct early hook + correct canvas discovery, or DO
// NOTHING. There is no post-layout scaling fallback of any kind.
#pragma once

#include <d3d9.h>

namespace selector {

void Init(const char* dllDir);
void OnDetach();

// Gate callbacks:
void OnStable(IDirect3DDevice9* dev, unsigned bbW, unsigned bbH);
void OnTick(IDirect3DDevice9* dev);
void OnReset();

}  // namespace selector
