#include "rage_wars_player_movement_speed_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "movement_speed.hpp"

#include <cstdint>
#include <cstring>

namespace {
using rage_wars::player::ObjectId;
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

    std::uint8_t load_byte(ObjectId object, std::uint32_t offset) const override {
        const auto address = argument(static_cast<std::uint32_t>(object + offset) ^ 3U);
        return *xr64_recomp_resolve_address(memory_, address);
    }

    void store_word(ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        const auto address = argument(static_cast<std::uint32_t>(object + offset));
        std::memcpy(xr64_recomp_resolve_address(memory_, address), &value, sizeof(value));
    }

    void store_byte(ObjectId object, std::uint32_t offset, std::uint8_t value) override {
        const auto address = argument(static_cast<std::uint32_t>(object + offset) ^ 3U);
        *xr64_recomp_resolve_address(memory_, address) = value;
    }

private:
    std::uint8_t* memory_;
};

bool enabled() {
    static const bool choice = xr64::rage_wars::recomp::feature_explicit_on("XR64_NATIVE_PLAYER_MOVEMENT_SPEED");
    return choice;
}
}

extern "C" int xr64_player_movement_speed_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || memory == nullptr || context == nullptr || !context->mips3_float_mode) return 0;

    const auto actor = static_cast<ObjectId>(context->r4);
    const auto movement_source = static_cast<ObjectId>(context->r5);
    const auto state_record = static_cast<ObjectId>(context->r6);
    const auto stack = static_cast<ObjectId>(context->r29);
    if (actor == 0 || movement_source == 0 || (actor & 3U) != 0 || (movement_source & 3U) != 0
            || (state_record != 0 && (state_record & 3U) != 0)
            || stack < 0x20U || (stack & 7U) != 0) return 0;

    RdramWords storage(memory);
    const auto frame = static_cast<ObjectId>(stack - 0x20U);
    // Preserve 002228E4's o32 save order and frame offsets.
    storage.store_word(frame, 0x10, static_cast<std::uint32_t>(context->r16));
    storage.store_word(frame, 0x18, static_cast<std::uint32_t>(context->r31));
    storage.store_word(frame, 0x14, static_cast<std::uint32_t>(context->r17));

    rage_wars::player::update_movement_speed(storage, actor, movement_source, state_record);

    // The original body moves a1 into a0 and restores s0, s1, ra and sp.
    context->r4 = argument(movement_source);
    context->r16 = static_cast<gpr>(static_cast<std::int32_t>(storage.load_word(frame, 0x10)));
    context->r17 = static_cast<gpr>(static_cast<std::int32_t>(storage.load_word(frame, 0x14)));
    context->r31 = static_cast<gpr>(static_cast<std::int32_t>(storage.load_word(frame, 0x18)));
    context->r29 = argument(stack);
    return 1;
}
