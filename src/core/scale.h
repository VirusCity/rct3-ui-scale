// core/scale.h — the uniform UI scale factor.
//
// CLAUDE.md rule: scale comes from EITHER the explicit user override OR
// backbuffer ÷ reference authoring dimension — never from a value that might
// itself already be scaled. Uniform() is a pure function of the CURRENT
// backbuffer dimensions + config, so the timing hook can compute it at the
// exact moment it validates a freshly-written canvas (which may be before the
// gate declares the backbuffer stable). Uniform scale (min of the two axes)
// keeps the canvas at the device aspect — non-uniform scale stretches the UI.
#pragma once

namespace scale {

// Returns the uniform magnification S (>= 1.0) for a backbuffer of w x h.
// S == 1.0 means "leave native — nothing to patch".
float Uniform(unsigned bbWidth, unsigned bbHeight);

}  // namespace scale
