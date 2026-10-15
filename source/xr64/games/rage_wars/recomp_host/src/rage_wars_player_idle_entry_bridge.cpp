#include "rage_wars_player_idle_entry_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "funcs.h"
#include "idle_entry.hpp"

#include <cstdint>
#include <cstring>

namespace {
using rage_wars::player::ObjectId;
using rage_wars::player::PlayerStateTransition;
using rage_wars::player::WordStorage;

gpr argument(std::uint32_t word) {
    return static_cast<gpr>(static_cast<std::int32_t>(word));
}

class RdramWords final : public WordStorage {
public:
    explicit RdramWords(std::uint8_t* memory) : memory_(memory) {}

    std::uint32_t load_word(ObjectId object, std::uint32_t offset) const override {
        std::uint32_t value{};
        const auto address = argument(static_cast<std::uint32_t>(object + offset));
        std::memcpy(&value, xr64_recomp_resolve_address(memory_, address), sizeof(value));
        return value;
    }

    void store_word(ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        const auto address = argument(static_cast<std::uint32_t>(object + offset));
        std::memcpy(xr64_recomp_resolve_address(memory_, address), &value, sizeof(value));
    }

private:
    std::uint8_t* memory_;
};

class GeneratedPlayerStateTransition final : public PlayerStateTransition {
public:
    GeneratedPlayerStateTransition(std::uint8_t* memory, recomp_context& context)
        : memory_(memory), context_(context) {}

    void set_state(ObjectId actor, std::uint32_t caller_context,
                   std::uint32_t state) override {
        context_.r4 = argument(actor);
        context_.r5 = argument(caller_context);
        context_.r6 = argument(state);
        trace_func_002227D0_r000233D0(memory_, &context_);
    }

private:
    std::uint8_t* memory_;
    recomp_context& context_;
};

bool enabled() {
    static const bool choice = xr64::rage_wars::recomp::feature_explicit_on("XR64_NATIVE_PLAYER_IDLE_ENTRY");
    return choice;
}
}

extern "C" int xr64_player_idle_entry_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || memory == nullptr || context == nullptr || !context->mips3_float_mode) return 0;

    const auto actor = static_cast<ObjectId>(context->r4);
    const auto caller_context = static_cast<std::uint32_t>(context->r5);
    const auto stack = static_cast<ObjectId>(context->r29);
    if (actor == 0 || (actor & 3U) != 0 || stack < 0x18U || (stack & 7U) != 0) return 0;

    RdramWords storage(memory);
    const auto frame = static_cast<ObjectId>(stack - 0x18U);
    // Preserve 0022C878's 0x18-byte frame and saved-RA slot.
    storage.store_word(frame, 0x10, static_cast<std::uint32_t>(context->r31));
    context->r29 = argument(frame);

    GeneratedPlayerStateTransition transition(memory, *context);
    rage_wars::player::enter_idle_state(storage, transition, actor, caller_context);

    context->r31 = static_cast<gpr>(static_cast<std::int32_t>(storage.load_word(frame, 0x10)));
    context->r29 = argument(stack);
    return 1;
}
