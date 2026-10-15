#include "ease_toward.hpp"
#include "recomp.h"

void resident_wave14_func_00274860(std::uint8_t*, recomp_context*);

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t ValueAddress = 0x80201000U;
constexpr std::uint32_t SnapAddress = 0x800C4894U;
constexpr std::uint32_t TimeAddress = 0x800CD648U;
constexpr std::size_t RamSize = 0x800000;
struct Store { std::uint32_t address; std::uint32_t value; bool operator==(const Store&) const = default; };
std::size_t physical(std::uint32_t address) {
    if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
    if (address < 0x00800000U) return address;
    throw std::runtime_error("unmapped test guest address");
}
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
};
thread_local World* ActiveOriginal = nullptr;
struct OriginalWord {
    std::uint32_t address;
    operator std::int32_t() const {
        if (ActiveOriginal == nullptr) throw std::runtime_error("missing original memory owner");
        return static_cast<std::int32_t>(ActiveOriginal->load_word(address, 0));
    }
    OriginalWord& operator=(std::uint32_t value) {
        if (ActiveOriginal == nullptr) throw std::runtime_error("missing original memory owner");
        ActiveOriginal->store_word(address, 0, value);
        return *this;
    }
};
OriginalWord original_word(std::uint8_t* rdram, gpr address) {
    if (ActiveOriginal == nullptr || ActiveOriginal->bytes.data() != rdram)
        throw std::runtime_error("original memory owner mismatch");
    return {static_cast<std::uint32_t>(address)};
}
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float number(std::uint32_t value) { return std::bit_cast<float>(value); }
struct Case { const char* name; std::uint32_t current, target, rate, dt, snap; };
void seed(World& world, const Case& c) {
    world.bytes.assign(RamSize, 0);
    world.stores.clear();
    world.seed_word(ValueAddress, c.current);
    world.seed_word(SnapAddress, c.snap);
    world.seed_word(TimeAddress, c.dt);
}
void run_original(World& world, const Case& c) {
    recomp_context context{};
    context.mips3_float_mode = 1;
    context.f_odd = &context.f1.u32l;
    context.r4 = static_cast<gpr>(ValueAddress);
    context.r5 = static_cast<gpr>(c.target);
    context.r6 = static_cast<gpr>(c.rate);
    context.r16 = 0x12345678;
    context.r17 = 0x23456789;
    context.r29 = static_cast<gpr>(0x807FF000U);
    context.r31 = static_cast<gpr>(0x3456789A);
    ActiveOriginal = &world;
    resident_wave14_func_00274860(world.bytes.data(), &context);
    ActiveOriginal = nullptr;
    if (context.r16 != static_cast<gpr>(0x12345678)
            || context.r17 != static_cast<gpr>(0x23456789)
            || context.r29 != static_cast<gpr>(0x807FF000U)
            || context.r31 != static_cast<gpr>(0x3456789A))
        throw std::runtime_error(std::string(c.name) + ": original ABI preservation failed");
}
void run_native(World& world, const Case& c) {
    rage_wars::player::ease_toward(world, ValueAddress, number(c.target), number(c.rate),
                                   number(c.snap), number(c.dt));
}
void compare(const Case& c) {
    World original, native;
    seed(original, c);
    native = original;
    run_original(original, c);
    run_native(native, c);
    if (original.bytes != native.bytes || original.stores != native.stores) {
        std::fprintf(stderr, "FAIL %s: memory/store sequence differs\n", c.name);
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
        Case{"rising_partial", bits(1.0F), bits(5.0F), bits(0.5F), bits(0.5F), bits(1.0F)},
        Case{"rising_overshoot", bits(4.5F), bits(5.0F), bits(2.0F), bits(1.0F), bits(1.0F)},
        Case{"falling_partial", bits(5.0F), bits(1.0F), bits(0.5F), bits(0.5F), bits(1.0F)},
        Case{"falling_overshoot", bits(1.0F), bits(0.0F), bits(2.0F), bits(1.0F), bits(1.0F)},
        Case{"snap_rate", bits(2.0F), bits(6.0F), bits(1.0F), bits(0.5F), bits(1.0F)},
        Case{"equal_non_snap", bits(4.0F), bits(4.0F), bits(0.5F), bits(0.5F), bits(1.0F)},
        Case{"zero_frame_time", bits(2.0F), bits(5.0F), bits(0.5F), bits(0.0F), bits(1.0F)},
        Case{"zero_rate", bits(2.0F), bits(5.0F), bits(0.0F), bits(0.5F), bits(1.0F)},
        Case{"negative_rate", bits(2.0F), bits(5.0F), bits(-0.5F), bits(0.5F), bits(1.0F)},
        Case{"nan_current", 0x7FC12345U, bits(5.0F), bits(0.5F), bits(0.5F), bits(1.0F)},
        Case{"nan_target", bits(2.0F), 0x7FC54321U, bits(0.5F), bits(0.5F), bits(1.0F)},
        Case{"snap_nan_current", 0x7FC12345U, bits(3.5F), bits(1.0F), bits(0.5F), bits(1.0F)},
    };
    for (const auto& c : cases) compare(c);
    std::printf("TOTAL_PASS %zu\n", cases.size());
}
