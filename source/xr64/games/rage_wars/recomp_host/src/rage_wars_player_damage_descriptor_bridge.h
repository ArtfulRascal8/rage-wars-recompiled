#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 only when the opt-in native 00216488 path ran. Otherwise the
// generated original body remains responsible for the call.
int xr64_player_damage_descriptor_try_update(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
