#pragma once
#include "recomp.h"
#ifdef __cplusplus
extern "C" {
#endif
// 1 means the scoped native attached-player update ran; 0 preserves the entire
// original body. Activation is an explicit experimental per-process choice.
int xr64_player_view_try_update(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif