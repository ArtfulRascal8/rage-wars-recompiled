#ifndef XR64_RAGE_WARS_NATIVE_PC_OPTIONS_H
#define XR64_RAGE_WARS_NATIVE_PC_OPTIONS_H

#include <stdint.h>
#include "recomp.h"
#include "rage_wars_native_pc_option_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t xr64_pause_focus_text(uint8_t* rdram, recomp_context* ctx, uint32_t node, uint32_t text);

void xr64_title_add_pc_options(uint8_t* rdram, recomp_context* ctx);
void xr64_title_remove_pc_options(uint8_t* rdram, recomp_context* ctx);
int xr64_title_handle_selection(uint8_t* rdram, recomp_context* ctx, uint32_t id);

void xr64_page16_add_pc_options(uint8_t* rdram, recomp_context* ctx,
                                uint32_t root);
void xr64_page16_remove_pc_options(uint8_t* rdram, recomp_context* ctx);
int xr64_page16_handle_selection(uint8_t* rdram, recomp_context* ctx,
                                 uint32_t id);
int xr64_page16_handle_back(uint8_t* rdram);
void xr64_page16_refresh_pc_options(uint8_t* rdram);
uint32_t xr64_controls_read_weapon_cycle(uint8_t* rdram,uint32_t actor);
void xr64_controls_weapon_cycle(uint8_t* rdram,recomp_context* ctx,uint32_t actor,uint32_t cycle);
void xr64_page16_restore_gameplay_music(uint8_t* rdram,recomp_context* ctx);
void xr64_pc_binding_select(uint32_t device,uint32_t action);
void xr64_pc_binding_cancel(void);
void xr64_pc_vr_action(void);
uint64_t xr64_pc_ui_revision(void);
uint32_t xr64_controls_spring_value(uint8_t* rdram,uint32_t actor,uint32_t original);

const char* xr64_pc_option_label(uint32_t option);
void xr64_pc_option_cycle(uint32_t option);

#ifdef __cplusplus
}
#endif

#endif
