#include "rage_wars_player_damage_descriptor_bridge.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint32_t RecordAddress = 0x80201000U;
constexpr std::uint32_t StackAddress = 0x807FE000U;
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
void require(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}
void seed() {
    std::fill(memory.begin(), memory.end(), 0);
    put(StackAddress + 0x10U, 0x89ABCDEFU);
    put(StackAddress + 0x14U, 0x76543210U);
}
void prepare_context(recomp_context& context) {
    context = {};
    context.r2 = 0x11223344;
    context.r3 = 0x22334455;
    context.r4 = static_cast<gpr>(RecordAddress);
    context.r5 = 0x01020304;
    context.r6 = 0xA0B0C0D0;
    context.r7 = 0x0F1E2D3C;
    context.r16 = 0x33445566;
    context.r29 = static_cast<gpr>(StackAddress);
    context.r31 = 0x44556677;
}
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* rdram, gpr address) {
    return rdram + physical(static_cast<std::uint32_t>(address));
}

int main(int argc, char**) {
    const bool expect_enabled = argc > 1;
    seed();
    recomp_context context{};
    prepare_context(context);
    const auto original_context = context;
    const auto original_memory = memory;
    const int handled = xr64_player_damage_descriptor_try_update(memory.data(), &context);
    if (!expect_enabled) {
        require(handled == 0 && std::memcmp(&context, &original_context, sizeof(context)) == 0
                && memory == original_memory, "default did not preserve generated body fallback");
        std::puts("PASS explicit opt-in is off by default; memory/context untouched");
        return 0;
    }

    require(handled == 1, "enabled bridge did not handle aligned arguments");
    require(get(RecordAddress + 0x00U) == 0x01020304U
            && get(RecordAddress + 0x04U) == 0xA0B0C0D0U
            && get(RecordAddress + 0x08U) == 0x0F1E2D3CU
            && get(RecordAddress + 0x0CU) == 0x89ABCDEFU
            && get(RecordAddress + 0x10U) == 0x76543210U, "five descriptor words differ");
    require(context.r2 == static_cast<gpr>(static_cast<std::int32_t>(0x89ABCDEFU))
            && context.r3 == static_cast<gpr>(static_cast<std::int32_t>(0x76543210U)),
            "v0/v1 outputs differ");
    require(context.r4 == original_context.r4 && context.r5 == original_context.r5
            && context.r6 == original_context.r6 && context.r7 == original_context.r7
            && context.r16 == original_context.r16 && context.r29 == original_context.r29
            && context.r31 == original_context.r31, "guest context preservation differs");

    prepare_context(context);
    context.r4 = 0x80201001U;
    const auto unaligned_context = context;
    const auto unaligned_memory = memory;
    require(xr64_player_damage_descriptor_try_update(memory.data(), &context) == 0,
            "unaligned destination did not fall back");
    require(std::memcmp(&context, &unaligned_context, sizeof(context)) == 0
            && memory == unaligned_memory, "unaligned fallback modified state");

    std::puts("PASS five stores, stack arguments, v0/v1 outputs, context preservation, alignment fallback");
}
