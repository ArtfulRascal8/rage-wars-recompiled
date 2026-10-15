#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 only when the opt-in native state-2 entry callback ran. Otherwise
// the generated original body remains responsible for the callback.
int xr64_player_idle_entry_try_update(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
