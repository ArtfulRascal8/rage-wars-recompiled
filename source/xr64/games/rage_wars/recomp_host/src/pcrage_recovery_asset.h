#pragma once

#include "recomp.h"

#include <cstdint>
#include <span>

#ifdef __cplusplus
extern "C" {
#endif

void pcrage_initialize_recovery_asset(void);
std::uint8_t *pcrage_recovery_memory(std::uint8_t *rdram, gpr guest_address);

// Existing checkpoint translation used by the resolver for descriptor fields
// and for addresses outside descriptor-backed guest ranges.
std::uint8_t *xr64_recomp_resolve_address_base(std::uint8_t *rdram, gpr address);

#ifdef __cplusplus
}
#endif

// The checkpoint already owns the validated, user-supplied ROM vector. This
// accessor publishes that same storage without creating another ROM store.
std::span<const std::uint8_t> xr64_recomp_rom();
