#include "rage_wars_player_floor_height_bridge.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint32_t ActorAddress = 0x80201000U;
constexpr std::uint32_t QueryAddress = 0x80201800U;
constexpr std::uint32_t FloorAddress = 0x80202000U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
constexpr std::uint32_t FactorAddress = 0x800C2C58U;
std::vector<std::uint8_t> memory(0x800000);
std::uint32_t lookup_result = 0;
std::uint32_t lookup_query = 0;
std::uint32_t lookup_stack = 0;
int lookup_calls = 0;

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
void seed(float current, float target, float factor) {
    std::fill(memory.begin(), memory.end(), 0);
    put(ActorAddress + 0x780U, bits(current));
    put(FloorAddress + 0x2CU, bits(target));
    put(FactorAddress, bits(factor));
    lookup_result = FloorAddress;
    lookup_calls = 0;
    lookup_query = lookup_stack = 0;
}
void prepare_context(recomp_context& context) {
    context = {};
    context.mips3_float_mode = 1;
    context.f_odd = &context.f1.u32l;
    context.r1 = 0x12345678;
    context.r2 = 0x23456789;
    context.r3 = 0x3456789A;
    context.r4 = static_cast<gpr>(ActorAddress);
    context.r5 = static_cast<gpr>(QueryAddress);
    context.r16 = 0x456789AB;
    context.r29 = static_cast<gpr>(static_cast<std::int32_t>(StackAddress));
    context.r31 = 0x56789ABC;
}
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* rdram, gpr address) {
    return rdram + physical(static_cast<std::uint32_t>(address));
}

extern "C" void trace_func_0024E7BC_r0004F3BC(std::uint8_t*, recomp_context* context) {
    ++lookup_calls;
    lookup_query = static_cast<std::uint32_t>(context->r4);
    lookup_stack = static_cast<std::uint32_t>(context->r29);
    context->r2 = static_cast<gpr>(lookup_result);
}

int main(int argc, char**) {
    const bool expect_enabled = argc > 1;
    seed(10.0F, 18.0F, 0.25F);
    recomp_context context{};
    prepare_context(context);
    const auto original_context = context;
    const auto original_memory = memory;
    const int handled = xr64_player_floor_height_try_update(memory.data(), &context);
    if (!expect_enabled) {
        require(handled == 0 && lookup_calls == 0
                && std::memcmp(&context, &original_context, sizeof(context)) == 0
                && memory == original_memory, "default did not preserve generated body fallback");
        std::puts("PASS explicit opt-in is off by default; memory/context untouched");
        return 0;
    }

    require(handled == 1 && lookup_calls == 1, "enabled bridge did not call generated floor lookup");
    require(lookup_query == QueryAddress && lookup_stack == StackAddress - 0x18U,
            "floor lookup arguments or outer frame differ");
    require(get(ActorAddress + 0x780U) == bits(12.0F), "fixed-factor floor blend differs");
    require(get(StackAddress - 8U) == static_cast<std::uint32_t>(original_context.r16)
            && get(StackAddress - 4U) == static_cast<std::uint32_t>(original_context.r31),
            "o32 saved-register frame stores differ");
    require(context.r2 == FloorAddress && context.r16 == original_context.r16
            && context.r29 == original_context.r29 && context.r31 == original_context.r31,
            "return pointer or restored registers differ");

    seed(10.0F, 18.0F, 0.25F);
    lookup_result = 0;
    prepare_context(context);
    require(xr64_player_floor_height_try_update(memory.data(), &context) == 1,
            "null floor lookup did not run the native path");
    require(get(ActorAddress + 0x780U) == bits(10.0F), "null floor lookup wrote actor height");
    require(context.r2 == 0, "null floor pointer was not returned in v0");

    seed(10.0F, 18.0F, 0.25F);
    prepare_context(context);
    context.mips3_float_mode = 0;
    const auto incompatible_context = context;
    const auto incompatible_memory = memory;
    require(xr64_player_floor_height_try_update(memory.data(), &context) == 0,
            "unsupported float mode did not fall back");
    require(std::memcmp(&context, &incompatible_context, sizeof(context)) == 0
            && memory == incompatible_memory, "float-mode fallback modified state");

    std::puts("PASS fixed-factor update, null lookup, generated callback frame, v0 return, restored registers, float-mode fallback");
}
