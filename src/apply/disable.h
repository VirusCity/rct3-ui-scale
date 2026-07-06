// apply/disable.h — the total safety net (CLAUDE.md Failure Conditions).
// Restores every patch, disables compensation, patches nothing further.
#pragma once

namespace disable {

// `reason` is logged with the diagnostics of what was searched and rejected.
void DisableScaling(const char* reason);

}  // namespace disable
