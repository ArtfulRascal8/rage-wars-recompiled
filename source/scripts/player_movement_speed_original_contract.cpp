#include "movement_speed.hpp"
#include "recomp.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

void trace_func_002228E4_r000234E4(std::uint8_t*, recomp_context*);
void resident_wave14_func_0024E60C(std::uint8_t*, recomp_context*);

namespace {
constexpr std::uint32_t ActorAddress = 0x80201000U;
constexpr std::uint32_t SourceAddress = 0x80202000U;
constexpr std::uint32_t DefinitionAddress = 0x80203000U;
constexpr std::uint32_t StateAddress = 0x80204000U;
constexpr std::uint32_t ProfileAddress = 0x80205000U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
constexpr std::uint32_t LowStateSpeedAddress = 0x800C2778U;
constexpr std::uint32_t AiClassZeroFirstAddress = 0x800C277CU;
constexpr std::uint32_t AiClassOneFirstAddress = 0x800C2780U;
constexpr std::uint32_t AiClassZeroSecondAddress = 0x800C2784U;
constexpr std::uint32_t AiClassOneSecondAddress = 0x800C2788U;
constexpr std::uint32_t GameConfigAddress = 0x80140208U;
constexpr std::size_t RamSize = 0x800000;

struct Store {
    std::uint32_t address;
    std::uint8_t width;
    std::uint32_t value;
    bool operator==(const Store&) const = default;
};

class World final : public rage_wars::player::WordStorage {
public:
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(RamSize);
    std::vector<Store> stores;

    std::uint32_t load_word(rage_wars::player::ObjectId object, std::uint32_t offset) const override {
        return guest_word(static_cast<std::uint32_t>(object + offset));
    }
    std::uint8_t load_byte(rage_wars::player::ObjectId object, std::uint32_t offset) const override {
        return guest_byte(static_cast<std::uint32_t>(object + offset));
    }
    std::uint16_t load_halfword(rage_wars::player::ObjectId object, std::uint32_t offset) const override {
        return guest_halfword(static_cast<std::uint32_t>(object + offset));
    }
    void store_word(rage_wars::player::ObjectId object, std::uint32_t offset,
                    std::uint32_t value) override {
        const auto address = static_cast<std::uint32_t>(object + offset);
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
        stores.push_back({address, 4, value});
    }
    void store_byte(rage_wars::player::ObjectId object, std::uint32_t offset,
                    std::uint8_t value) override {
        const auto address = static_cast<std::uint32_t>(object + offset);
        bytes[physical(address ^ 3U)] = value;
        stores.push_back({address, 1, value});
    }

    [[nodiscard]] std::uint32_t guest_word(std::uint32_t address) const {
        std::uint32_t value{};
        std::memcpy(&value, bytes.data() + physical(address), sizeof(value));
        return value;
    }
    [[nodiscard]] std::uint8_t guest_byte(std::uint32_t address) const {
        return bytes[physical(address ^ 3U)];
    }
    [[nodiscard]] std::uint16_t guest_halfword(std::uint32_t address) const {
        std::uint16_t value{};
        std::memcpy(&value, bytes.data() + physical(address ^ 2U), sizeof(value));
        return value;
    }
    void seed_word(std::uint32_t address, std::uint32_t value) {
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
    }
    void seed_byte(std::uint32_t address, std::uint8_t value) {
        bytes[physical(address ^ 3U)] = value;
    }
    void seed_halfword(std::uint32_t address, std::uint16_t value) {
        std::memcpy(bytes.data() + physical(address ^ 2U), &value, sizeof(value));
    }

private:
    static std::size_t physical(std::uint32_t address) {
        if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
        if (address < 0x00800000U) return address;
        throw std::runtime_error("unmapped differential guest address");
    }
};

thread_local World* active_original = nullptr;

void require_original_memory(std::uint8_t* rdram) {
    if (active_original == nullptr || active_original->bytes.data() != rdram)
        throw std::runtime_error("generated body memory owner mismatch");
}

struct OriginalWord {
    std::uint8_t* rdram;
    std::uint32_t address;
    operator std::int32_t() const {
        require_original_memory(rdram);
        return static_cast<std::int32_t>(active_original->guest_word(address));
    }
    OriginalWord& operator=(std::uint32_t value) {
        require_original_memory(rdram);
        active_original->store_word(address, 0, value);
        return *this;
    }
};
struct OriginalByte {
    std::uint8_t* rdram;
    std::uint32_t address;
    operator std::uint8_t() const {
        require_original_memory(rdram);
        return active_original->guest_byte(address);
    }
    OriginalByte& operator=(std::uint8_t value) {
        require_original_memory(rdram);
        active_original->store_byte(address, 0, value);
        return *this;
    }
};
struct OriginalHalfword {
    std::uint8_t* rdram;
    std::uint32_t address;
    operator std::uint16_t() const {
        require_original_memory(rdram);
        return active_original->guest_halfword(address);
    }
};

OriginalWord original_word(std::uint8_t* rdram, gpr address) {
    require_original_memory(rdram);
    return {rdram, static_cast<std::uint32_t>(address)};
}
OriginalByte original_byte(std::uint8_t* rdram, gpr address) {
    require_original_memory(rdram);
    return {rdram, static_cast<std::uint32_t>(address)};
}
OriginalHalfword original_halfword(std::uint8_t* rdram, gpr address) {
    require_original_memory(rdram);
    return {rdram, static_cast<std::uint32_t>(address)};
}

struct Case {
    const char* name;
    std::uint16_t actor_state;
    bool ai;
    std::uint8_t speed_class;
    bool has_state_record;
    bool config_enabled;
    std::uint32_t source_flags;
    std::uint8_t source_type;
    std::uint16_t state_flags;
    float base_speed;
    float low_state_speed;
    float ai_zero_first;
    float ai_one_first;
    float ai_zero_second;
    float ai_one_second;
    float config_factor;
    float state_factor;
};

std::uint32_t float_bits(float value) { return std::bit_cast<std::uint32_t>(value); }
gpr guest_argument(std::uint32_t value) {
    return static_cast<gpr>(static_cast<std::int32_t>(value));
}

void seed(World& world, const Case& c) {
    world.bytes.assign(RamSize, 0);
    world.stores.clear();
    world.seed_halfword(ActorAddress + 0x650U, c.actor_state);
    world.seed_word(ActorAddress + 0x1450U, c.ai ? 1U : 0U);
    world.seed_word(ActorAddress + 0x5D8U, ProfileAddress);
    world.seed_word(ActorAddress + 0x784U, float_bits(-9.0F));
    world.seed_word(SourceAddress + 0x18U, DefinitionAddress);
    world.seed_word(SourceAddress + 0x38U, c.source_flags);
    world.seed_byte(SourceAddress, c.source_type);
    world.seed_word(DefinitionAddress + 0x1CU, float_bits(c.base_speed));
    world.seed_byte(ProfileAddress + 0x93U, c.speed_class);
    world.seed_halfword(StateAddress + 0x52U, c.state_flags);
    world.seed_word(StateAddress + 0x30U, float_bits(c.state_factor));
    world.seed_word(LowStateSpeedAddress, float_bits(c.low_state_speed));
    world.seed_word(AiClassZeroFirstAddress, float_bits(c.ai_zero_first));
    world.seed_word(AiClassOneFirstAddress, float_bits(c.ai_one_first));
    world.seed_word(AiClassZeroSecondAddress, float_bits(c.ai_zero_second));
    world.seed_word(AiClassOneSecondAddress, float_bits(c.ai_one_second));
    world.seed_byte(GameConfigAddress + 0x1DU, c.config_enabled ? 1U : 0U);
    world.seed_word(GameConfigAddress + 0x20U, float_bits(c.config_factor));
}

void set_args(recomp_context& context, const Case& c) {
    context = {};
    context.mips3_float_mode = 1;
    context.f_odd = &context.f1.u32l;
    context.r1 = 0x12345678U;
    context.r2 = 0x23456789U;
    context.r3 = 0x3456789AU;
    context.r4 = guest_argument(ActorAddress);
    context.r5 = guest_argument(SourceAddress);
    context.r6 = guest_argument(c.has_state_record ? StateAddress : 0U);
    context.r16 = 0x456789ABU;
    context.r17 = 0x56789ABCU;
    context.r29 = guest_argument(StackAddress);
    context.r31 = 0x6789ABCDU;
}

void compare(const Case& c) {
    World original, native;
    seed(original, c);
    native = original;
    recomp_context original_context{}, native_context{};
    set_args(original_context, c);
    set_args(native_context, c);
    active_original = &original;
    trace_func_002228E4_r000234E4(original.bytes.data(), &original_context);
    active_original = nullptr;

    const auto initial_context = native_context;
    const auto frame = StackAddress - 0x20U;
    native.store_word(frame, 0x10U, static_cast<std::uint32_t>(initial_context.r16));
    native.store_word(frame, 0x18U, static_cast<std::uint32_t>(initial_context.r31));
    native.store_word(frame, 0x14U, static_cast<std::uint32_t>(initial_context.r17));
    rage_wars::player::update_movement_speed(
        native, ActorAddress, SourceAddress, c.has_state_record ? StateAddress : 0U);
    native_context.r4 = guest_argument(SourceAddress);
    native_context.r16 = static_cast<gpr>(static_cast<std::int32_t>(native.guest_word(frame + 0x10U)));
    native_context.r17 = static_cast<gpr>(static_cast<std::int32_t>(native.guest_word(frame + 0x14U)));
    native_context.r31 = static_cast<gpr>(static_cast<std::int32_t>(native.guest_word(frame + 0x18U)));
    native_context.r29 = guest_argument(StackAddress);

    const bool memory_matches = original.bytes == native.bytes;
    const bool stores_match = original.stores == native.stores;
    const bool preserved_registers_match = original_context.r4 == native_context.r4
            && original_context.r16 == native_context.r16 && original_context.r17 == native_context.r17
            && original_context.r29 == native_context.r29 && original_context.r31 == native_context.r31;
    if (!memory_matches || !stores_match || !preserved_registers_match) {
        std::fprintf(stderr, "FAIL %s: memory=%d stores=%d preserved_registers=%d (stores %zu/%zu)\n",
                     c.name, memory_matches, stores_match, preserved_registers_match,
                     original.stores.size(), native.stores.size());
        std::exit(1);
    }
    std::printf("PASS %s\n", c.name);
}
}

#undef MEM_W
#undef MEM_HU
#undef MEM_BU
#undef MEM_B
#define MEM_W(offset, reg) original_word(rdram, (reg) + (offset))
#define MEM_HU(offset, reg) original_halfword(rdram, (reg) + (offset))
#define MEM_BU(offset, reg) original_byte(rdram, (reg) + (offset))
#define MEM_B(offset, reg) original_byte(rdram, (reg) + (offset))
#include "generated_ground_contact.inc"
#include "generated_original.inc"

int main() {
    const std::array cases{
        Case{"low_state_constant", 1, false, 0, false, false, 1, 0, 0, 5.0F, 0.75F,
             2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 2.0F},
        Case{"human_definition_base", 2, false, 0, false, false, 1, 0, 0, 5.0F, 0.75F,
             2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 2.0F},
        Case{"ai_class_zero", 2, true, 0, false, false, 1, 0, 0, 5.0F, 0.75F,
             2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 2.0F},
        Case{"ai_class_one", 2, true, 1, false, false, 1, 0, 0, 5.0F, 0.75F,
             2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 2.0F},
        Case{"ai_class_two_skips_profile_scaling", 2, true, 2, false, false, 1, 0, 0, 5.0F, 0.75F,
             2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 2.0F},
        Case{"invalid_ai_class_resets_and_uses_class_zero", 2, true, 9, false, false, 1, 0, 0, 5.0F,
             0.75F, 2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 2.0F},
        Case{"global_config_after_ai_scaling", 2, true, 0, false, true, 1, 0, 0, 5.0F,
             0.75F, 2.0F, 4.0F, 3.0F, 0.5F, 1.25F, 2.0F},
        Case{"ground_contact_applies_state_factor", 2, false, 0, true, false, 4, 1, 0, 5.0F,
             0.75F, 2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 2.0F},
        Case{"state_flag_applies_without_ground_contact", 2, false, 0, true, false, 0, 0, 0x800,
             5.0F, 0.75F, 2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 0.5F},
        Case{"source_flag_blocks_state_factor", 2, false, 0, true, false, 1, 1, 0x800, 5.0F,
             0.75F, 2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 0.5F},
        Case{"null_state_record_skips_contact", 2, false, 0, false, false, 4, 1, 0, 5.0F,
             0.75F, 2.0F, 4.0F, 3.0F, 0.5F, 1.5F, 0.5F},
    };
    for (const auto& c : cases) compare(c);
    std::printf("TOTAL_PASS %zu\n", cases.size());
}
