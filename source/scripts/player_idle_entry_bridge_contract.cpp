#include "rage_wars_player_idle_entry_bridge.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint32_t ActorAddress = 0x80201000U;
constexpr std::uint32_t CallerContextAddress = 0x80202000U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
std::vector<std::uint8_t> memory(0x800000);
struct Call { std::uint32_t actor, caller_context, state, stack; };
std::vector<Call> calls;

std::size_t physical(std::uint32_t address) {
    if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
    if (address < 0x00800000U) return address;
    std::abort();
}
void put_word(std::uint32_t address, std::uint32_t value) {
    std::memcpy(memory.data() + physical(address), &value, sizeof(value));
}
std::uint32_t get_word(std::uint32_t address) {
    std::uint32_t value{};
    std::memcpy(&value, memory.data() + physical(address), sizeof(value));
    return value;
}
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
gpr argument(std::uint32_t value) {
    return static_cast<gpr>(static_cast<std::int32_t>(value));
}
void require(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}
void seed() {
    std::fill(memory.begin(), memory.end(), 0);
    put_word(ActorAddress + 0x6C0U, bits(0.0F));
    put_word(ActorAddress + 0x6C4U, bits(1.0F));
    calls.clear();
}
void prepare_context(recomp_context& context) {
    context = {};
    context.mips3_float_mode = 1;
    context.f_odd = &context.f1.u32l;
    context.r1 = 0x11223344U;
    context.r2 = 0x22334455U;
    context.r3 = 0x33445566U;
    context.r4 = argument(ActorAddress);
    context.r5 = argument(CallerContextAddress);
    context.r6 = 0x44556677U;
    context.r16 = 0x55667788U;
    context.r17 = 0x66778899U;
    context.r29 = argument(StackAddress);
    context.r31 = 0x778899AAU;
}
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* rdram, gpr address) {
    return rdram + physical(static_cast<std::uint32_t>(address));
}

extern "C" void trace_func_002227D0_r000233D0(std::uint8_t*, recomp_context* context) {
    calls.push_back({static_cast<std::uint32_t>(context->r4),
                     static_cast<std::uint32_t>(context->r5),
                     static_cast<std::uint32_t>(context->r6),
                     static_cast<std::uint32_t>(context->r29)});
}

int main(int argc, char**) {
    const bool expect_enabled = argc > 1;
    seed();
    recomp_context context{};
    prepare_context(context);
    const auto original_context = context;
    const auto original_memory = memory;
    const int handled = xr64_player_idle_entry_try_update(memory.data(), &context);
    if (!expect_enabled) {
        require(handled == 0 && calls.empty()
                && std::memcmp(&context, &original_context, sizeof(context)) == 0
                && memory == original_memory,
                "default did not preserve generated idle-entry fallback");
        std::puts("PASS explicit opt-in is off by default; memory/context untouched");
        return 0;
    }

    require(handled == 1 && calls.size() == 1, "enabled idle-entry path did not call Player_SetState");
    require(calls[0].actor == ActorAddress && calls[0].caller_context == CallerContextAddress
            && calls[0].state == 3U && calls[0].stack == StackAddress - 0x18U,
            "Player_SetState arguments or callback stack frame differ");
    require(get_word(StackAddress - 8U) == static_cast<std::uint32_t>(original_context.r31),
            "saved-RA frame store differs");
    require(context.r4 == original_context.r4 && context.r5 == original_context.r5
            && context.r6 == 3U && context.r16 == original_context.r16
            && context.r17 == original_context.r17 && context.r29 == original_context.r29
            && context.r31 == original_context.r31,
            "entry arguments or restored registers differ");

    seed();
    prepare_context(context);
    context.mips3_float_mode = 0;
    const auto unsupported_context = context;
    const auto unsupported_memory = memory;
    require(xr64_player_idle_entry_try_update(memory.data(), &context) == 0,
            "unsupported floating-point mode did not fall back");
    require(calls.empty() && std::memcmp(&context, &unsupported_context, sizeof(context)) == 0
            && memory == unsupported_memory,
            "floating-point fallback modified guest state");

    std::puts("PASS state-3 callback arguments, o32 frame, restored registers, and float-mode fallback");
}
