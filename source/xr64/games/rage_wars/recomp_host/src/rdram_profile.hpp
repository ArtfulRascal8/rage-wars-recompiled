#pragma once

#include <cstddef>
#include <cstdint>

namespace xr64::rage_wars::memory_profile {

#ifndef XR64_RAGE_WARS_RDRAM_SIZE_BYTES
#define XR64_RAGE_WARS_RDRAM_SIZE_BYTES 0x00400000U
#endif

inline constexpr std::size_t kRdramSize = XR64_RAGE_WARS_RDRAM_SIZE_BYTES;
static_assert(kRdramSize == 0x00400000U || kRdramSize == 0x00800000U,
        "Rage Wars RDRAM must use the 4 MiB base or 8 MiB Expansion Pak profile");

inline constexpr std::uint32_t kPhysicalMask =
        static_cast<std::uint32_t>(kRdramSize - 1U);
inline constexpr std::uint32_t kKseg0End =
        0x80000000U + static_cast<std::uint32_t>(kRdramSize);
inline constexpr std::uint32_t kKseg1End =
        0xA0000000U + static_cast<std::uint32_t>(kRdramSize);

} // namespace xr64::rage_wars::memory_profile
