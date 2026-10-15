#include "rage_wars_player_surface_lookup_bridge.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint32_t ObjectAddress = 0x80201000U;
constexpr std::uint32_t TableAddress = 0x80119DC8U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
std::vector<std::uint8_t> memory(0x800000);
std::uint32_t result_pointer = 0;
std::uint32_t observed_table = 0;
std::uint32_t observed_selector = 0;
std::uint32_t observed_stack = 0;
int calls = 0;

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
void seed(std::uint32_t selector, std::uint32_t pointer) {
    std::fill(memory.begin(), memory.end(), 0);
    put(ObjectAddress + 0x14U, selector);
    result_pointer = pointer;
    observed_table = observed_selector = observed_stack = 0;
    calls = 0;
}
void prepare_context(recomp_context& context) {
    context = {};
    context.r1 = 0x12345678;
    context.r2 = 0x23456789;
    context.r3 = 0x3456789A;
    context.r4 = static_cast<gpr>(static_cast<std::int32_t>(ObjectAddress));
    context.r5 = 0x456789AB;
    context.r29 = static_cast<gpr>(static_cast<std::int32_t>(StackAddress));
    context.r31 = 0x6789ABCD;
}
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* rdram, gpr address) {
    return rdram + physical(static_cast<std::uint32_t>(address));
}

extern "C" void resident_wave14_func_0028B248(std::uint8_t*, recomp_context* context) {
    ++calls;
    observed_table = static_cast<std::uint32_t>(context->r4);
    observed_selector = static_cast<std::uint32_t>(context->r5);
    observed_stack = static_cast<std::uint32_t>(context->r29);
    context->r2 = static_cast<gpr>(result_pointer);
}

int main(int argc, char**) {
    const bool expect_enabled = argc > 1;
    seed(0x80000001U, 0x80203000U);
    recomp_context context{};
    prepare_context(context);
    const auto original_context = context;
    const auto original_memory = memory;
    const int handled = xr64_player_surface_lookup_try_update(memory.data(), &context);
    if (!expect_enabled) {
        require(handled == 0 && calls == 0
                && std::memcmp(&context, &original_context, sizeof(context)) == 0
                && memory == original_memory, "default did not preserve generated body fallback");
        std::puts("PASS explicit opt-in is off by default; memory/context untouched");
        return 0;
    }

    require(handled == 1 && calls == 1, "enabled bridge did not call the generated table lookup");
    require(observed_table == TableAddress && observed_selector == 0x80000001U
            && observed_stack == StackAddress - 0x18U, "generated callback arguments/frame differ");
    require(get(StackAddress - 8U) == static_cast<std::uint32_t>(original_context.r31),
            "o32 return-address frame store differs");
    require(context.r2 == 0x80203000U && context.r5 == static_cast<gpr>(static_cast<std::int32_t>(0x80000001U))
            && context.r4 == static_cast<gpr>(static_cast<std::int32_t>(TableAddress)),
            "v0/a0/a1 results differ");
    require(context.r29 == original_context.r29 && context.r31 == original_context.r31,
            "stack or return address was not restored");

    seed(0, 0x80203000U);
    prepare_context(context);
    require(xr64_player_surface_lookup_try_update(memory.data(), &context) == 1,
            "zero-selector native path was not handled");
    require(calls == 0 && context.r2 == 0 && context.r5 == 0U
            && context.r4 == original_context.r4, "zero-selector path did not skip the table call");
    require(get(StackAddress - 8U) == static_cast<std::uint32_t>(original_context.r31),
            "zero-selector frame store differs");

    std::puts("PASS selector branch, direct generated callback, frame stores, v0/a0/a1, restored sp/ra");
}
