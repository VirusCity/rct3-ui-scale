#include "disable.h"

#include "../core/log.h"
#include "../core/patch.h"
#include "../core/state.h"

namespace disable {

void DisableScaling(const char* reason) {
  LOG("disable: DisableScaling — %s", reason);
  state::Set(State::Failed);
  patch::RestoreAll();
  LOG("disable: all raw patches restored (%zu active after restore); "
      "lifecycle hooks remain as passive observers",
      patch::ActiveCount());
}

}  // namespace disable
