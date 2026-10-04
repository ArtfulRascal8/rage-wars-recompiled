#pragma once

#include "recomp.h"

#include <cstddef>

void rage_wars_register_generated_overlays();
std::size_t rage_wars_generated_section_count();

extern "C" bool rage_wars_surface_unresolved_callable_fault(
        unsigned char *rdram, recomp_context *ctx, unsigned int vram);
extern "C" bool rage_wars_validate_declared_entry_provenance(
        unsigned char *rdram, int vram, recomp_func_t *declared_function);

void rage_wars_gate5_main_handoff(
        unsigned char *rdram, recomp_context *ctx);

