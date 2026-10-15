#include "rage_wars_player_ease_bridge.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint32_t ValueAddress = 0x80201000U;
constexpr std::uint32_t SnapAddress = 0x800C4894U;
constexpr std::uint32_t TimeAddress = 0x800CD648U;
std::vector<std::uint8_t> memory(0x800000);

std::size_t physical(std::uint32_t address) {
    if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
    if (address < 0x00800000U) return address;
    std::abort();
}
void put(std::uint32_t address, std::uint32_t value) {
    std::memcpy(memory.data() + physical(address), &value, sizeof(value));
}
std::uint32_t get(std::uint32_t address) {
    std::uint32_t value{};
    std::memcpy(&value, memory.data() + physical(address), sizeof(value));
    return value;
}
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
void require(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}
void seed(float value, float dt = 0.5F) {
    std::fill(memory.begin(), memory.end(), 0);
    put(ValueAddress, bits(value));
    put(SnapAddress, bits(1.0F));
    put(TimeAddress, bits(dt));
}
void prepare_context(recomp_context& result, float target, float rate) {
    result = {};
    result.mips3_float_mode = 1;
    result.f_odd = &result.f1.u32l;
    result.r4 = static_cast<gpr>(ValueAddress);
    result.r5 = static_cast<gpr>(bits(target));
    result.r6 = static_cast<gpr>(bits(rate));
    result.r16 = 0x12345678;
    result.r29 = 0x807FF000;
    result.r31 = 0x87654321;
}
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* rdram, gpr address) {
    return rdram + physical(static_cast<std::uint32_t>(address));
}

int main(int argc, char**) {
    const bool expect_enabled = argc > 1;
    seed(1.0F);
    recomp_context ctx{};
    prepare_context(ctx, 5.0F, 0.5F);
    const auto original_context = ctx;
    const auto original_memory = memory;
    const int handled = xr64_player_ease_try_update(memory.data(), &ctx);
    require(std::memcmp(&ctx, &original_context, sizeof(ctx)) == 0, "guest context changed");
    if (!expect_enabled) {
        require(handled == 0 && memory == original_memory, "default did not preserve generated body fallback");
        std::puts("PASS explicit opt-in is off by default; memory/context untouched");
        return 0;
    }
    require(handled == 1 && get(ValueAddress) == bits(2.0F), "dt-scaled rise differs");

    seed(4.5F, 1.0F);
    prepare_context(ctx, 5.0F, 2.0F);
    const auto overshoot_context = ctx;
    require(xr64_player_ease_try_update(memory.data(), &ctx) == 1, "overshoot path not handled");
    require(get(ValueAddress) == bits(5.0F), "positive overshoot was not clamped");
    require(std::memcmp(&ctx, &overshoot_context, sizeof(ctx)) == 0, "overshoot changed guest context");

    seed(5.0F, 1.0F);
    prepare_context(ctx, 1.0F, 2.0F);
    require(xr64_player_ease_try_update(memory.data(), &ctx) == 1, "falling path not handled");
    require(get(ValueAddress) == bits(1.0F), "negative overshoot was not clamped");

    seed(2.0F, 0.5F);
    prepare_context(ctx, 3.25F, 1.0F);
    require(xr64_player_ease_try_update(memory.data(), &ctx) == 1, "snap path not handled");
    require(get(ValueAddress) == bits(3.25F), "snap-rate path did not assign the target");

    seed(2.0F, 0.5F);
    prepare_context(ctx, 2.0F, 0.5F);
    const auto equal_memory = memory;
    require(xr64_player_ease_try_update(memory.data(), &ctx) == 1, "equal path not handled");
    require(memory == equal_memory, "equal values unexpectedly wrote memory");

    seed(1.0F);
    prepare_context(ctx, 5.0F, 0.5F);
    ctx.mips3_float_mode = 0;
    const auto incompatible_context = ctx;
    const auto incompatible_memory = memory;
    require(xr64_player_ease_try_update(memory.data(), &ctx) == 0, "unsupported float mode did not fall back");
    require(std::memcmp(&ctx, &incompatible_context, sizeof(ctx)) == 0 && memory == incompatible_memory,
            "float-mode fallback modified state");

    std::puts("PASS dt scaling, both overshoot clamps, snap rate, equal target, context restore, float-mode fallback");
}
