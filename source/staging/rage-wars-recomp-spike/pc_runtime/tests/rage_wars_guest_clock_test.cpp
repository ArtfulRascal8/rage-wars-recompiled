#include "rage_wars_guest_clock.hpp"
#include <array>
#include <cstdint>
#include <cstdio>

int main() {
    using namespace xr64::rage_wars::guest_clock;
    if (ticks_from_microseconds(1000) != 46875 || ticks_from_microseconds(1000000) != 46875000) return 1;
    // Exercise both halves of the o32 return value, CP0 sign changes, and
    // several complete wraps without relying on intervening reads or VI ticks.
    constexpr std::array<std::uint64_t, 10> samples{
        0, 1, 0x7FFFFFFFULL, 0x80000000ULL, 0xFFFFFFFFULL,
        0x100000000ULL, 0x100000030ULL, 0x800000000ULL,
        0x7FFFFFFF80000000ULL, 0x80000000FFFFFFFFULL};
    for (auto ticks : samples) {
        recomp_context ctx{}; ctx.r4=0x1234; ctx.r29=0x5678;
        write_os_time_result(ctx, ticks);
        const auto returned = (std::uint64_t(std::uint32_t(ctx.r2)) << 32) | std::uint32_t(ctx.r3);
        if (returned != ticks || ctx.r4 != 0x1234 || ctx.r29 != 0x5678) return 2;
        if (ctx.r2 != static_cast<gpr>(static_cast<std::int32_t>(ticks >> 32)) ||
            ctx.r3 != static_cast<gpr>(static_cast<std::int32_t>(ticks))) return 3;
    }
    auto before=ticks_from_microseconds(91625968), after=ticks_from_microseconds(91625970);
    if (before >= 0x100000000ULL || after <= 0x100000000ULL || after-before != 93) return 4;
    if (std::uint32_t(after) >= std::uint32_t(before)) return 5;
    if (ticks_from_microseconds(1000000000000ULL) != 46875000000000ULL) return 6;
    std::puts("PASS: guest time remains 64-bit across counter wraps and sparse reads; o32 return preserves other registers.");
}
