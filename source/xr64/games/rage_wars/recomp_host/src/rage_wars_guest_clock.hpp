#pragma once

#include <cstdint>
#include "recomp.h"

namespace xr64::rage_wars::guest_clock {

// The R4300 Count register advances at 46,875,000 Hz. Keep the complete
// elapsed time for osGetTime; truncation is only valid for the CP0 Count ABI.
constexpr std::uint64_t ticks_from_microseconds(std::uint64_t micros) {
    return (micros / 1000U) * 46875U + ((micros % 1000U) * 46875U) / 1000U;
}

inline void write_os_time_result(recomp_context &ctx, std::uint64_t ticks) {
    // The game's o32 ABI returns a 64-bit integer in v0:v1, with each
    // 32-bit register result sign-extended into the host GPR representation.
    ctx.r2 = static_cast<gpr>(static_cast<std::int32_t>(ticks >> 32U));
    ctx.r3 = static_cast<gpr>(static_cast<std::int32_t>(ticks));
}

}  // namespace xr64::rage_wars::guest_clock
