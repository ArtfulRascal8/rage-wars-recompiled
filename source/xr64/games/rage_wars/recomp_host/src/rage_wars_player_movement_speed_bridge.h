#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 only when the opt-in native 002228E4 path ran. Otherwise the
// generated original body remains responsible for the update.
int xr64_player_movement_speed_try_update(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
