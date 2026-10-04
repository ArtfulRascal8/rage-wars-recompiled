#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

int xr64_audio_queue_guest(uint8_t *rdram, recomp_context *ctx);
int xr64_body_local_view_enabled(uint8_t *rdram, gpr actor, gpr view);
void xr64_body_begin_view(void);
void xr64_native_menu_trace(const char *event, uint8_t *rdram,
        gpr value0, gpr value1, gpr value2);
uint32_t xr64_pause_focus_text(uint8_t *rdram, recomp_context *ctx, uint32_t node, uint32_t text);
void xr64_title_add_pc_options(uint8_t* rdram, recomp_context* ctx);
void xr64_title_remove_pc_options(uint8_t* rdram, recomp_context* ctx);
int xr64_title_handle_selection(uint8_t* rdram, recomp_context* ctx, uint32_t id);

void xr64_page16_add_pc_options(uint8_t *rdram, recomp_context *ctx,
        uint32_t root);
void xr64_page16_remove_pc_options(uint8_t *rdram, recomp_context *ctx);
int xr64_page16_handle_selection(uint8_t *rdram, recomp_context *ctx,
        uint32_t id);

int xr64_page16_handle_back(uint8_t *rdram);
void xr64_page16_refresh_pc_options(uint8_t* rdram);
uint32_t xr64_controls_take_weapon_cycle(void);
uint32_t xr64_controls_read_weapon_cycle(uint8_t* rdram,uint32_t actor);
void xr64_controls_weapon_cycle(uint8_t* rdram,recomp_context* ctx,uint32_t actor,uint32_t cycle);
void xr64_page16_restore_gameplay_music(uint8_t* rdram,recomp_context* ctx);
void xr64_pc_binding_select(uint32_t device,uint32_t action);
void xr64_pc_binding_cancel(void);
uint64_t xr64_pc_ui_revision(void);
uint32_t xr64_controls_spring_value(uint8_t* rdram,uint32_t actor,uint32_t original);

void xr64_xr_observe_player(uint8_t *rdram, uint32_t actor);
void xr64_controls_begin_player(uint8_t *rdram, uint32_t actor);
void xr64_controls_publish_player(uint8_t *rdram, uint32_t actor);
int xr64_controls_apply_yaw(uint8_t *rdram, uint32_t actor, uint32_t view, uint32_t config);
int xr64_controls_apply_pitch(uint8_t *rdram, uint32_t actor, uint32_t view, uint32_t config);
int xr64_xr_shot_override(uint8_t *rdram, uint32_t actor, uint32_t action, uint32_t *shot_quaternion, const uint32_t *actor_position, uint32_t *weapon_position, uint32_t *target_position);


int xr64_rage_wars_native_os_get_time(uint8_t *rdram, recomp_context *ctx);

uint8_t *xr64_recomp_resolve_address(uint8_t *rdram, gpr address);

#ifdef __cplusplus
}
#endif

#undef MEM_W
#undef MEM_H
#undef MEM_B
#undef MEM_HU
#undef MEM_BU

#define MEM_W(offset, reg) \
    (*(int32_t *)xr64_recomp_resolve_address(rdram, (gpr)((reg) + (offset))))

#define MEM_H(offset, reg) \
    (*(int16_t *)xr64_recomp_resolve_address(rdram, (gpr)(((reg) + (offset)) ^ 2)))

#define MEM_B(offset, reg) \
    (*(int8_t *)xr64_recomp_resolve_address(rdram, (gpr)(((reg) + (offset)) ^ 3)))

#define MEM_HU(offset, reg) \
    (*(uint16_t *)xr64_recomp_resolve_address(rdram, (gpr)(((reg) + (offset)) ^ 2)))

#define MEM_BU(offset, reg) \
    (*(uint8_t *)xr64_recomp_resolve_address(rdram, (gpr)(((reg) + (offset)) ^ 3)))

// recomp.h defines these helpers before this forced-include shim replaces the
// memory macros, so its inline bodies retain the direct KSEG0-only mapping.
// Keep the complete unaligned 32-bit access group on the same translator as
// ordinary generated loads and stores, including KUSEG/TLB-backed records.
static inline gpr xr64_do_lwl(uint8_t *rdram, gpr initial_value, gpr offset, gpr reg) {
    const gpr address = offset + reg;
    const gpr word_address = address & ~((gpr)0x3);
    uint32_t loaded_value = (uint32_t)MEM_W(0, word_address);
    const gpr misalignment = address & 0x3;
    const gpr masked_value = initial_value &
            (gpr)(uint32_t)~(0xFFFFFFFFu << (misalignment * 8));
    loaded_value <<= misalignment * 8;
    return (gpr)(int32_t)(masked_value | loaded_value);
}

static inline gpr xr64_do_lwr(uint8_t *rdram, gpr initial_value, gpr offset, gpr reg) {
    const gpr address = offset + reg;
    const gpr word_address = address & ~((gpr)0x3);
    uint32_t loaded_value = (uint32_t)MEM_W(0, word_address);
    const gpr misalignment = address & 0x3;
    const gpr masked_value = initial_value &
            (gpr)(uint32_t)~(0xFFFFFFFFu >> (24 - misalignment * 8));
    loaded_value >>= 24 - misalignment * 8;
    return (gpr)(int32_t)(masked_value | loaded_value);
}

static inline void xr64_do_swl(uint8_t *rdram, gpr offset, gpr reg, gpr value) {
    const gpr address = offset + reg;
    const gpr word_address = address & ~((gpr)0x3);
    const uint32_t initial_value = (uint32_t)MEM_W(0, word_address);
    const gpr misalignment = address & 0x3;
    const uint32_t masked_initial_value =
            initial_value & ~(0xFFFFFFFFu >> (misalignment * 8));
    const uint32_t shifted_input_value =
            ((uint32_t)value) >> (misalignment * 8);
    MEM_W(0, word_address) = masked_initial_value | shifted_input_value;
}

static inline void xr64_do_swr(uint8_t *rdram, gpr offset, gpr reg, gpr value) {
    const gpr address = offset + reg;
    const gpr word_address = address & ~((gpr)0x3);
    const uint32_t initial_value = (uint32_t)MEM_W(0, word_address);
    const gpr misalignment = address & 0x3;
    const uint32_t masked_initial_value =
            initial_value & ~(0xFFFFFFFFu << (24 - misalignment * 8));
    const uint32_t shifted_input_value =
            ((uint32_t)value) << (24 - misalignment * 8);
    MEM_W(0, word_address) = masked_initial_value | shifted_input_value;
}

#define do_lwl xr64_do_lwl
#define do_lwr xr64_do_lwr
#define do_swl xr64_do_swl
#define do_swr xr64_do_swr
