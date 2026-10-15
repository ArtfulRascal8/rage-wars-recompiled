#include "damage_descriptor.hpp"
#include "recomp.h"

void trace_func_00216488_r00017088(std::uint8_t*, recomp_context*);

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t RecordAddress = 0x80201000U;
constexpr std::uint32_t StackAddress = 0x807FE000U;
constexpr std::size_t RamSize = 0x800000;
struct Store { std::uint32_t address; std::uint32_t value; bool operator==(const Store&) const = default; };
class World final : public rage_wars::player::WordStorage {
public:
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(RamSize);
    std::vector<Store> stores;
    std::uint32_t load_word(rage_wars::player::ObjectId object, std::uint32_t offset) const override {
        const auto address = static_cast<std::uint32_t>(object + offset);
        std::uint32_t value{};
        std::memcpy(&value, bytes.data() + physical(address), sizeof(value));
        return value;
    }
    void store_word(rage_wars::player::ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        const auto address = static_cast<std::uint32_t>(object + offset);
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
        stores.push_back({address, value});
    }
    void seed_word(std::uint32_t address, std::uint32_t value) {
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
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
struct Case {
    const char* name;
    rage_wars::player::DamageDescriptorWords words;
};
void seed(World& world, const Case& c) {
    world.bytes.assign(RamSize, 0);
    world.stores.clear();
    world.seed_word(StackAddress + 0x10U, c.words[3]);
    world.seed_word(StackAddress + 0x14U, c.words[4]);
}
void set_args(recomp_context& context, const Case& c) {
    context = {};
    context.r2 = 0x11223344;
    context.r3 = 0x22334455;
    context.r4 = static_cast<gpr>(RecordAddress);
    context.r5 = static_cast<gpr>(c.words[0]);
    context.r6 = static_cast<gpr>(c.words[1]);
    context.r7 = static_cast<gpr>(c.words[2]);
    context.r16 = 0x33445566;
    context.r29 = static_cast<gpr>(StackAddress);
    context.r31 = 0x44556677;
}
void compare(const Case& c) {
    World original, native;
    seed(original, c);
    native = original;
    recomp_context original_context{}, native_context{};
    set_args(original_context, c);
    set_args(native_context, c);
    active_original = &original;
    trace_func_00216488_r00017088(original.bytes.data(), &original_context);
    active_original = nullptr;
    rage_wars::player::write_damage_descriptor(native, RecordAddress, c.words);
    native_context.r2 = static_cast<gpr>(static_cast<std::int32_t>(c.words[3]));
    native_context.r3 = static_cast<gpr>(static_cast<std::int32_t>(c.words[4]));
    if (original.bytes != native.bytes || original.stores != native.stores
            || std::memcmp(&original_context, &native_context, sizeof(original_context)) != 0) {
        std::fprintf(stderr, "FAIL %s: generated body and native helper differ\n", c.name);
        std::exit(1);
    }
    std::printf("PASS %s\n", c.name);
}
}

#undef MEM_W
#define MEM_W(offset, reg) original_word(rdram, (reg) + (offset))
#include "generated_original.inc"

int main() {
    const std::array cases{
        Case{"reference_damage_payload", {0U, 0x0003E700U, 0x41CCCCCCU, 128U, 0U}},
        Case{"mixed_signed_and_flag_words", {0xFFFFFFFFU, 0x80000001U, 0x7FC12345U, 0xA5A5A5A5U, 0x5A5A5A5AU}},
        Case{"all_zero_words", {0U, 0U, 0U, 0U, 0U}},
    };
    for (const auto& c : cases) compare(c);
    std::printf("TOTAL_PASS %zu\n", cases.size());
}
