#include "idle_entry.hpp"
#include "recomp.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

void trace_func_0022C878_r0002D478(std::uint8_t*, recomp_context*);
void trace_func_002227D0_r000233D0(std::uint8_t*, recomp_context*);

namespace {
constexpr std::uint32_t ActorAddress = 0x80201000U;
constexpr std::uint32_t CallerContextAddress = 0x80202000U;
constexpr std::uint32_t StackAddress = 0x807FF000U;
constexpr std::size_t RamSize = 0x800000;

struct Store {
    std::uint32_t address;
    std::uint32_t value;
    bool operator==(const Store&) const = default;
};
struct StateCall {
    std::uint32_t actor;
    std::uint32_t caller_context;
    std::uint32_t state;
    std::uint32_t stack;
    bool operator==(const StateCall&) const = default;
};

class World final : public rage_wars::player::WordStorage {
public:
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(RamSize);
    std::vector<Store> stores;
    std::vector<std::uint32_t> reads;

    std::uint32_t load_word(rage_wars::player::ObjectId object, std::uint32_t offset) const override {
        auto* self = const_cast<World*>(this);
        const auto address = static_cast<std::uint32_t>(object + offset);
        self->reads.push_back(address);
        return guest_word(address);
    }
    void store_word(rage_wars::player::ObjectId object, std::uint32_t offset,
                    std::uint32_t value) override {
        const auto address = static_cast<std::uint32_t>(object + offset);
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
        stores.push_back({address, value});
    }
    [[nodiscard]] std::uint32_t guest_word(std::uint32_t address) const {
        std::uint32_t value{};
        std::memcpy(&value, bytes.data() + physical(address), sizeof(value));
        return value;
    }
    void seed_word(std::uint32_t address, std::uint32_t value) {
        std::memcpy(bytes.data() + physical(address), &value, sizeof(value));
    }

private:
    static std::size_t physical(std::uint32_t address) {
        if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
        if (address < 0x00800000U) return address;
        throw std::runtime_error("unmapped differential guest address");
    }
};

thread_local World* active_original = nullptr;
thread_local std::vector<StateCall>* original_state_calls = nullptr;

void require_original_memory(std::uint8_t* rdram) {
    if (active_original == nullptr || active_original->bytes.data() != rdram)
        throw std::runtime_error("generated idle-entry body memory owner mismatch");
}

struct OriginalWord {
    std::uint8_t* rdram;
    std::uint32_t address;
    operator std::int32_t() const {
        require_original_memory(rdram);
        active_original->reads.push_back(address);
        return static_cast<std::int32_t>(active_original->guest_word(address));
    }
    OriginalWord& operator=(std::uint32_t value) {
        require_original_memory(rdram);
        active_original->store_word(address, 0, value);
        return *this;
    }
};

OriginalWord original_word(std::uint8_t* rdram, gpr address) {
    require_original_memory(rdram);
    return {rdram, static_cast<std::uint32_t>(address)};
}

std::size_t physical(std::uint32_t address) {
    if (address >= 0x80000000U && address < 0x80800000U) return address - 0x80000000U;
    if (address < 0x00800000U) return address;
    throw std::runtime_error("unmapped differential callback address");
}
gpr argument(std::uint32_t value) {
    return static_cast<gpr>(static_cast<std::int32_t>(value));
}

struct Case {
    const char* name;
    float forward_speed;
    float strafe_speed;
    bool expect_state_change;
};

class FixtureStateTransition final : public rage_wars::player::PlayerStateTransition {
public:
    FixtureStateTransition(recomp_context& context, std::vector<StateCall>& calls)
        : context_(context), calls_(calls) {}

    void set_state(rage_wars::player::ObjectId actor, std::uint32_t caller_context,
                   std::uint32_t state) override {
        context_.r4 = argument(actor);
        context_.r5 = argument(caller_context);
        context_.r6 = argument(state);
        calls_.push_back({actor, caller_context, state, static_cast<std::uint32_t>(context_.r29)});
    }

private:
    recomp_context& context_;
    std::vector<StateCall>& calls_;
};

void seed(World& world, const Case& c) {
    world.bytes.assign(RamSize, 0);
    world.stores.clear();
    world.reads.clear();
    world.seed_word(ActorAddress + 0x6C0U, std::bit_cast<std::uint32_t>(c.forward_speed));
    world.seed_word(ActorAddress + 0x6C4U, std::bit_cast<std::uint32_t>(c.strafe_speed));
}

void set_args(recomp_context& context) {
    context = {};
    context.mips3_float_mode = 1;
    context.f_odd = &context.f1.u32l;
    context.r1 = 0x10203040U;
    context.r2 = 0x20304050U;
    context.r3 = 0x30405060U;
    context.r4 = argument(ActorAddress);
    context.r5 = argument(CallerContextAddress);
    context.r6 = 0x40506070U;
    context.r16 = 0x50607080U;
    context.r17 = 0x60708090U;
    context.r29 = argument(StackAddress);
    context.r31 = 0x708090A0U;
}

void compare(const Case& c) {
    World original, native;
    seed(original, c);
    native = original;
    recomp_context original_context{}, native_context{};
    set_args(original_context);
    set_args(native_context);
    std::vector<StateCall> original_calls, native_calls;

    active_original = &original;
    original_state_calls = &original_calls;
    trace_func_0022C878_r0002D478(original.bytes.data(), &original_context);
    original_state_calls = nullptr;
    active_original = nullptr;

    const auto initial_context = native_context;
    const auto frame = StackAddress - 0x18U;
    native.store_word(frame, 0x10U, static_cast<std::uint32_t>(initial_context.r31));
    native_context.r29 = argument(frame);
    FixtureStateTransition transition(native_context, native_calls);
    rage_wars::player::enter_idle_state(native, transition, ActorAddress, CallerContextAddress);
    native_context.r31 = static_cast<gpr>(static_cast<std::int32_t>(native.load_word(frame, 0x10U)));
    native_context.r29 = argument(StackAddress);

    const bool memory_matches = original.bytes == native.bytes;
    const bool stores_match = original.stores == native.stores;
    const bool reads_match = original.reads == native.reads;
    const bool calls_match = original_calls == native_calls;
    const bool registers_match = original_context.r4 == native_context.r4
            && original_context.r5 == native_context.r5 && original_context.r6 == native_context.r6
            && original_context.r16 == native_context.r16 && original_context.r17 == native_context.r17
            && original_context.r29 == native_context.r29 && original_context.r31 == native_context.r31;
    const bool call_expectation = original_calls.size() == (c.expect_state_change ? 1U : 0U)
            && (!c.expect_state_change || (original_calls[0].state == 3U
                && original_calls[0].actor == ActorAddress
                && original_calls[0].caller_context == CallerContextAddress
                && original_calls[0].stack == frame));
    if (!memory_matches || !stores_match || !reads_match || !calls_match
            || !registers_match || !call_expectation) {
        std::fprintf(stderr,
                     "FAIL %s: memory=%d stores=%d reads=%d calls=%d regs=%d expectation=%d\n",
                     c.name, memory_matches, stores_match, reads_match, calls_match,
                     registers_match, call_expectation);
        std::exit(1);
    }
    std::printf("PASS %s\n", c.name);
}
}

void trace_func_002227D0_r000233D0(std::uint8_t*, recomp_context* context) {
    if (active_original == nullptr || original_state_calls == nullptr)
        throw std::runtime_error("missing generated state-setter fixture");
    original_state_calls->push_back({static_cast<std::uint32_t>(context->r4),
                                     static_cast<std::uint32_t>(context->r5),
                                     static_cast<std::uint32_t>(context->r6),
                                     static_cast<std::uint32_t>(context->r29)});
}

#undef MEM_W
#define MEM_W(offset, reg) original_word(rdram, (reg) + (offset))
#include "generated_original.inc"

int main() {
    const auto nan = std::bit_cast<float>(0x7FC00001U);
    const std::array cases{
        Case{"both_planar_speeds_zero", 0.0F, 0.0F, false},
        Case{"forward_motion_transitions_to_run", 1.0F, 0.0F, true},
        Case{"strafe_motion_transitions_to_run", 0.0F, 1.0F, true},
        Case{"negative_strafe_transitions_to_run", 0.0F, -0.5F, true},
        Case{"signed_zero_is_stationary", -0.0F, -0.0F, false},
        Case{"nan_forward_compares_not_equal_to_zero", nan, 0.0F, true},
    };
    for (const auto& c : cases) compare(c);
    std::printf("TOTAL_PASS %zu\n", cases.size());
}
