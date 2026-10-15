#include "surface_lookup.hpp"
#include "recomp.h"

void trace_func_0024E7BC_r0004F3BC(std::uint8_t*, recomp_context*);
void resident_wave14_func_0028B248(std::uint8_t*, recomp_context*);

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t ObjectAddress = 0x80201000U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
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
thread_local std::uint32_t lookup_result = 0;
thread_local std::uint32_t lookup_table = 0;
thread_local std::uint32_t lookup_selector = 0;
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
struct Case { const char* name; std::uint32_t selector, result; };
void seed(World& world, const Case& c) {
    world.bytes.assign(RamSize, 0);
    world.stores.clear();
    world.seed_word(ObjectAddress + 0x14U, c.selector);
    lookup_result = c.result;
    lookup_table = lookup_selector = 0;
}
void set_args(recomp_context& context) {
    context = {};
    context.r1 = 0x12345678;
    context.r2 = 0x23456789;
    context.r3 = 0x3456789A;
    context.r4 = static_cast<gpr>(static_cast<std::int32_t>(ObjectAddress));
    context.r5 = 0x456789AB;
    context.r16 = 0x56789ABC;
    context.r29 = static_cast<gpr>(static_cast<std::int32_t>(StackAddress));
    context.r31 = 0x6789ABCD;
}
class FixtureLookup final : public rage_wars::player::SurfaceTableLookup {
public:
    rage_wars::player::ObjectId query_table(rage_wars::player::ObjectId table,
                                             std::uint32_t selector) override {
        lookup_table = table;
        lookup_selector = selector;
        return lookup_result;
    }
};
void compare(const Case& c) {
    World original, native;
    seed(original, c);
    native = original;
    recomp_context original_context{}, native_context{};
    set_args(original_context);
    set_args(native_context);
    active_original = &original;
    trace_func_0024E7BC_r0004F3BC(original.bytes.data(), &original_context);
    active_original = nullptr;

    const auto frame = StackAddress - 0x18U;
    native.store_word(frame, 0x10U, static_cast<std::uint32_t>(native_context.r31));
    FixtureLookup fixture;
    const auto result = rage_wars::player::surface_lookup(native, fixture, ObjectAddress);
    native_context.r29 = static_cast<gpr>(static_cast<std::int32_t>(StackAddress));
    native_context.r31 = static_cast<gpr>(static_cast<std::int32_t>(native.load_word(frame, 0x10U)));
    native_context.r5 = static_cast<gpr>(static_cast<std::int32_t>(result.selector));
    native_context.r2 = result.queried_table ? static_cast<gpr>(result.surface) : 0;
    if (result.queried_table) native_context.r4 = static_cast<gpr>(static_cast<std::int32_t>(0x80119DC8U));
    if (original.bytes != native.bytes || original.stores != native.stores
            || original_context.r1 != native_context.r1 || original_context.r2 != native_context.r2
            || original_context.r4 != native_context.r4 || original_context.r5 != native_context.r5
            || original_context.r29 != native_context.r29 || original_context.r31 != native_context.r31
            || (result.queried_table && (lookup_table != 0x80119DC8U || lookup_selector != c.selector))) {
        std::fprintf(stderr, "FAIL %s: generated body and native helper differ\n", c.name);
        std::exit(1);
    }
    std::printf("PASS %s\n", c.name);
}
}

void resident_wave14_func_0028B248(std::uint8_t*, recomp_context* context) {
    lookup_table = static_cast<std::uint32_t>(context->r4);
    lookup_selector = static_cast<std::uint32_t>(context->r5);
    context->r2 = static_cast<gpr>(lookup_result);
}

#undef MEM_W
#define MEM_W(offset, reg) original_word(rdram, (reg) + (offset))
#include "generated_original.inc"

int main() {
    const std::array cases{
        Case{"zero_selector_skips_table", 0U, 0x80203000U},
        Case{"nonzero_selector_null_result", 1U, 0U},
        Case{"high_bit_selector_returns_pointer", 0x80000001U, 0x80203000U},
    };
    for (const auto& c : cases) compare(c);
    std::printf("TOTAL_PASS %zu\n", cases.size());
}
