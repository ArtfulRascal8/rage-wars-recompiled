#include "ground_contact.hpp"
#include "recomp.h"

void resident_wave14_func_0024E60C(std::uint8_t*, recomp_context*);

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t ObjectAddress = 0x80201000U;
constexpr std::size_t RamSize = 0x800000;
class World final : public rage_wars::player::WordStorage {
public:
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(RamSize);
    std::uint32_t load_word(rage_wars::player::ObjectId object, std::uint32_t offset) const override {
        const auto address = static_cast<std::uint32_t>(object + offset);
        std::uint32_t value{};
        std::memcpy(&value, bytes.data() + physical(address), sizeof(value));
        return value;
    }
    void store_word(rage_wars::player::ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        const auto address = static_cast<std::uint32_t>(object + offset);
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
    }
    void seed_word(std::uint32_t address, std::uint32_t value) {
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
    }
    void seed_byte(std::uint32_t address, std::uint8_t value) {
        bytes[physical(address ^ 3U)] = value;
    }
private:
    static std::size_t physical(std::uint32_t address) {
        if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
        if (address < 0x00800000U) return address;
        throw std::runtime_error("unmapped test guest address");
    }
};
thread_local World* active_original = nullptr;
struct OriginalWord {
    std::uint32_t address;
    operator std::int32_t() const {
        if (!active_original) throw std::runtime_error("missing original memory owner");
        return static_cast<std::int32_t>(active_original->load_word(address, 0));
    }
    OriginalWord& operator=(std::uint32_t value) {
        if (!active_original) throw std::runtime_error("missing original memory owner");
        active_original->store_word(address, 0, value);
        return *this;
    }
};
OriginalWord original_word(std::uint8_t* rdram, gpr address) {
    if (!active_original || active_original->bytes.data() != rdram)
        throw std::runtime_error("original memory owner mismatch");
    return {static_cast<std::uint32_t>(address)};
}
std::uint8_t original_byte(std::uint8_t* rdram, gpr address) {
    if (!active_original || active_original->bytes.data() != rdram)
        throw std::runtime_error("original memory owner mismatch");
    return active_original->load_byte(static_cast<std::uint32_t>(address), 0);
}
struct Case { const char* name; std::uint8_t type; std::uint32_t contact; };
void seed(World& world, const Case& c) {
    world.bytes.assign(RamSize, 0);
    world.seed_byte(ObjectAddress, c.type);
    world.seed_word(ObjectAddress + 0x38U, c.contact);
}
void set_args(recomp_context& context) {
    context = {};
    context.r2 = 0x11223344;
    context.r3 = 0x22334455;
    context.r4 = static_cast<gpr>(ObjectAddress);
    context.r16 = 0x33445566;
    context.r29 = static_cast<gpr>(0x807FF000U);
    context.r31 = static_cast<gpr>(0x44556677);
}
void compare(const Case& c) {
    World original, native;
    seed(original, c);
    native = original;
    recomp_context original_context{}, native_context{};
    set_args(original_context);
    set_args(native_context);
    active_original = &original;
    resident_wave14_func_0024E60C(original.bytes.data(), &original_context);
    active_original = nullptr;
    const auto result = rage_wars::player::ground_contact(native, ObjectAddress);
    native_context.r2 = static_cast<gpr>(result.contact_code);
    native_context.r3 = static_cast<gpr>(result.object_type);
    if (original.bytes != native.bytes
            || std::memcmp(&original_context, &native_context, sizeof(original_context)) != 0) {
        std::fprintf(stderr, "FAIL %s: generated body and native helper differ\n", c.name);
        std::exit(1);
    }
    std::printf("PASS %s\n", c.name);
}
}

#undef MEM_W
#undef MEM_BU
#define MEM_W(offset, reg) original_word(rdram, (reg) + (offset))
#define MEM_BU(offset, reg) original_byte(rdram, (reg) + (offset))
#include "generated_original.inc"

int main() {
    const std::array cases{
        Case{"type_one_masks_low_bits", 1, 0xA5A5A5AEU},
        Case{"type_one_zero_contact", 1, 0xFFFFFFF8U},
        Case{"type_zero_returns_zero", 0, 7U},
        Case{"type_two_returns_zero", 2, 6U},
        Case{"type_255_returns_zero", 255, 5U},
    };
    for (const auto& c : cases) compare(c);
    std::printf("TOTAL_PASS %zu\n", cases.size());
}
