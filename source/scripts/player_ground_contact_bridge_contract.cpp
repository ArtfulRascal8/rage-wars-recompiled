#include "rage_wars_player_ground_contact_bridge.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr std::uint32_t ObjectAddress = 0x80201000U;
std::vector<std::uint8_t> memory(0x800000);

std::size_t physical(std::uint32_t address) {
    if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
    if (address < 0x00800000U) return address;
    std::abort();
}
void put_word(std::uint32_t address, std::uint32_t value) {
    std::memcpy(memory.data() + physical(address), &value, sizeof(value));
}
void put_byte(std::uint32_t address, std::uint8_t value) {
    memory[physical(address ^ 3U)] = value;
}
void require(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}
void seed(std::uint8_t type, std::uint32_t contact) {
    std::fill(memory.begin(), memory.end(), 0);
    put_byte(ObjectAddress, type);
    put_word(ObjectAddress + 0x38U, contact);
}
void prepare_context(recomp_context& context) {
    context = {};
    context.r2 = 0x11223344;
    context.r3 = 0x22334455;
    context.r4 = static_cast<gpr>(ObjectAddress);
    context.r16 = 0x33445566;
    context.r29 = 0x807FF000;
    context.r31 = 0x44556677;
}
}

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* rdram, gpr address) {
    return rdram + physical(static_cast<std::uint32_t>(address));
}

int main(int argc, char**) {
    const bool expect_enabled = argc > 1;
    seed(1, 0xA5A5A5AEU);
    recomp_context context{};
    prepare_context(context);
    const auto original_context = context;
    const auto original_memory = memory;
    const int handled = xr64_player_ground_contact_try_update(memory.data(), &context);
    if (!expect_enabled) {
        require(handled == 0 && std::memcmp(&context, &original_context, sizeof(context)) == 0
                && memory == original_memory, "default did not preserve generated body fallback");
        std::puts("PASS explicit opt-in is off by default; memory/context untouched");
        return 0;
    }

    require(handled == 1, "enabled bridge did not handle aligned object");
    require(context.r2 == 6 && context.r3 == 1, "type-1 contact result differs");
    require(context.r16 == original_context.r16 && context.r29 == original_context.r29
            && context.r31 == original_context.r31, "guest preserved registers changed");
    require(memory == original_memory, "query unexpectedly wrote guest memory");

    seed(2, 7U);
    prepare_context(context);
    require(xr64_player_ground_contact_try_update(memory.data(), &context) == 1,
            "non-type-1 bridge did not handle aligned object");
    require(context.r2 == 0 && context.r3 == 2, "non-type-1 result differs");

    prepare_context(context);
    context.r4 = 0x80201001U;
    const auto unaligned_context = context;
    const auto unaligned_memory = memory;
    require(xr64_player_ground_contact_try_update(memory.data(), &context) == 0,
            "unaligned object did not fall back");
    require(std::memcmp(&context, &unaligned_context, sizeof(context)) == 0
            && memory == unaligned_memory, "unaligned fallback modified state");

    std::puts("PASS type byte, conditional +0x38 read, low-three-bit mask, v0/v1 outputs, context preservation, alignment fallback");
}
