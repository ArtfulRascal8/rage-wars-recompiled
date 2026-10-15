#include "rage_wars_player_movement_speed_bridge.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint32_t ActorAddress = 0x80201000U;
constexpr std::uint32_t SourceAddress = 0x80202000U;
constexpr std::uint32_t DefinitionAddress = 0x80203000U;
constexpr std::uint32_t StateAddress = 0x80204000U;
constexpr std::uint32_t ProfileAddress = 0x80205000U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
constexpr std::uint32_t GameConfigAddress = 0x80140208U;
std::vector<std::uint8_t> memory(0x800000);

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
void put_byte(std::uint32_t address, std::uint8_t value) {
    memory[physical(address ^ 3U)] = value;
}
std::uint8_t get_byte(std::uint32_t address) {
    return memory[physical(address ^ 3U)];
}
void put_halfword(std::uint32_t address, std::uint16_t value) {
    std::memcpy(memory.data() + physical(address ^ 2U), &value, sizeof(value));
}
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
gpr argument(std::uint32_t word) {
    return static_cast<gpr>(static_cast<std::int32_t>(word));
}
void require(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}
void seed() {
    std::fill(memory.begin(), memory.end(), 0);
    put_halfword(ActorAddress + 0x650U, 2);
    put_word(ActorAddress + 0x1450U, 1);
    put_word(ActorAddress + 0x5D8U, ProfileAddress);
    put_word(ActorAddress + 0x784U, bits(-9.0F));
    put_word(SourceAddress + 0x18U, DefinitionAddress);
    put_word(SourceAddress + 0x38U, 1U);
    put_word(DefinitionAddress + 0x1CU, bits(5.0F));
    put_byte(ProfileAddress + 0x93U, 9U);
    put_word(0x800C277CU, bits(2.0F));
    put_word(0x800C2784U, bits(3.0F));
    put_byte(GameConfigAddress + 0x1DU, 0);
}
void prepare_context(recomp_context& context) {
    context = {};
    context.mips3_float_mode = 1;
    context.f_odd = &context.f1.u32l;
    context.r1 = 0x11223344U;
    context.r2 = 0x22334455U;
    context.r3 = 0x33445566U;
    context.r4 = argument(ActorAddress);
    context.r5 = argument(SourceAddress);
    context.r6 = argument(StateAddress);
    context.r16 = 0x44556677U;
    context.r17 = 0x55667788U;
    context.r29 = argument(StackAddress);
    context.r31 = 0x66778899U;
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
    const int handled = xr64_player_movement_speed_try_update(memory.data(), &context);
    if (!expect_enabled) {
        require(handled == 0 && std::memcmp(&context, &original_context, sizeof(context)) == 0
                && memory == original_memory,
                "default did not preserve generated original fallback");
        std::puts("PASS explicit opt-in is off by default; memory/context untouched");
        return 0;
    }

    require(handled == 1, "enabled movement-speed bridge did not handle aligned input");
    require(get_word(ActorAddress + 0x784U) == bits(30.0F),
            "invalid AI speed class did not reset and apply both class-zero factors");
    require(get_byte(ProfileAddress + 0x93U) == 0,
            "invalid profile speed class was not written back as zero");
    require(get_word(StackAddress - 0x10U) == static_cast<std::uint32_t>(original_context.r16)
            && get_word(StackAddress - 0x08U) == static_cast<std::uint32_t>(original_context.r31)
            && get_word(StackAddress - 0x0CU) == static_cast<std::uint32_t>(original_context.r17),
            "o32 saved-register stores or offsets differ");
    require(context.r4 == argument(SourceAddress)
            && context.r16 == original_context.r16 && context.r17 == original_context.r17
            && context.r29 == original_context.r29 && context.r31 == original_context.r31,
            "entry argument or restored callee-saved registers differ");

    seed();
    prepare_context(context);
    context.mips3_float_mode = 0;
    const auto unsupported_context = context;
    const auto unsupported_memory = memory;
    require(xr64_player_movement_speed_try_update(memory.data(), &context) == 0,
            "unsupported floating-point mode did not fall back");
    require(std::memcmp(&context, &unsupported_context, sizeof(context)) == 0
            && memory == unsupported_memory,
            "floating-point fallback modified guest state");

    std::puts("PASS AI profile reset, movement-speed stores, frame preservation, and float-mode fallback");
}
