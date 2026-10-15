#pragma once
#include "recomp.h"
#ifdef __cplusplus
extern "C" {
#endif
// 1 means the native 002233CC ground movement update ran; 0 preserves the entire
// original body. Activation is an explicit experimental per-process choice.
int xr64_ground_movement_try_update(uint8_t* rdram, recomp_context* ctx);
// VR analog input: stick rotated by the movement-reference yaw into the game's
// forward/strafe axes. 0 until both axis signs are learned from digital input.
int xr64_ground_movement_analog(float stick_x, float stick_y, float yaw, float field_forward,
                                float field_strafe, float* forward, float* strafe);
#ifdef __cplusplus
}
#endif
