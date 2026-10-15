#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 only when the opt-in native 0024E7BC path ran. Otherwise the
// generated original body remains responsible for the call.
int xr64_player_surface_lookup_try_update(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
