#include "floor_height.hpp"
#include "recomp.h"

void trace_func_0022C054_r0002CC54(std::uint8_t*, recomp_context*);
void trace_func_0024E7BC_r0004F3BC(std::uint8_t*, recomp_context*);

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t ActorAddress = 0x80201000U;
constexpr std::uint32_t QueryAddress = 0x80201800U;
constexpr std::uint32_t FloorAddress = 0x80202000U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
constexpr std::uint32_t FactorAddress = 0x800C2C58U;
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
thread_local std::uint32_t floor_result = 0;
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
struct Case { const char* name; float current, target, factor; std::uint32_t floor; };
void seed(World& world, const Case& c) {
    world.bytes.assign(RamSize, 0);
    world.stores.clear();
    world.seed_word(ActorAddress + 0x780U, std::bit_cast<std::uint32_t>(c.current));
    world.seed_word(FloorAddress + 0x2CU, std::bit_cast<std::uint32_t>(c.target));
    world.seed_word(FactorAddress, std::bit_cast<std::uint32_t>(c.factor));
    floor_result = c.floor;
}
void set_args(recomp_context& context) {
    context = {};
    context.mips3_float_mode = 1;
    context.f_odd = &context.f1.u32l;
    context.r1 = 0x12345678;
    context.r2 = 0x23456789;
    context.r3 = 0x3456789A;
    context.r4 = static_cast<gpr>(static_cast<std::int32_t>(ActorAddress));
    context.r5 = static_cast<gpr>(static_cast<std::int32_t>(QueryAddress));
    context.r16 = 0x456789AB;
    context.r29 = static_cast<gpr>(static_cast<std::int32_t>(StackAddress));
    context.r31 = 0x56789ABC;
}
void compare(const Case& c) {
    World original, native;
    seed(original, c);
    native = original;
    recomp_context original_context{}, native_context{};
    set_args(original_context);
    set_args(native_context);
    active_original = &original;
    trace_func_0022C054_r0002CC54(original.bytes.data(), &original_context);
    active_original = nullptr;

    native.store_word(StackAddress - 0x18U, 0x10U, static_cast<std::uint32_t>(native_context.r16));
    native.store_word(StackAddress - 0x18U, 0x14U, static_cast<std::uint32_t>(native_context.r31));
    class FixedLookup final : public rage_wars::player::FloorObjectLookup {
    public:
        rage_wars::player::ObjectId find_floor(rage_wars::player::ObjectId) override {
            return floor_result;
        }
    } lookup;
    const auto floor = rage_wars::player::update_floor_height(native, lookup, ActorAddress, QueryAddress);
    native_context.r4 = static_cast<gpr>(static_cast<std::int32_t>(QueryAddress));
    native_context.r29 = static_cast<gpr>(static_cast<std::int32_t>(StackAddress));
    native_context.r2 = static_cast<gpr>(floor);
    if (floor != 0) native_context.r1 = static_cast<gpr>(static_cast<std::int32_t>(0x800C0000U));
    if (original.bytes != native.bytes || original.stores != native.stores
            || original_context.r1 != native_context.r1 || original_context.r2 != native_context.r2
            || original_context.r4 != native_context.r4 || original_context.r16 != native_context.r16
            || original_context.r29 != native_context.r29 || original_context.r31 != native_context.r31) {
        std::fprintf(stderr, "FAIL %s: generated body and native helper differ\n", c.name);
        std::exit(1);
    }
    std::printf("PASS %s\n", c.name);
}
}

void trace_func_0024E7BC_r0004F3BC(std::uint8_t*, recomp_context* context) {
    context->r2 = static_cast<gpr>(floor_result);
}

#undef MEM_W
#define MEM_W(offset, reg) original_word(rdram, (reg) + (offset))
#include "generated_original.inc"

int main() {
    const std::array cases{
        Case{"partial_fixed_factor", 10.0F, 18.0F, 0.25F, FloorAddress},
        Case{"overshoot_is_not_clamped", 10.0F, 18.0F, 2.0F, FloorAddress},
        Case{"zero_factor_still_stores", 10.0F, 18.0F, 0.0F, FloorAddress},
        Case{"null_floor_only_saves_frame", 10.0F, 18.0F, 0.25F, 0U},
    };
    for (const auto& c : cases) compare(c);
    std::printf("TOTAL_PASS %zu\n", cases.size());
}
