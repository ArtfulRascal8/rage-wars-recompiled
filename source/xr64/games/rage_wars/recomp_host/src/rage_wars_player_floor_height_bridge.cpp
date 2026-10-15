#include "rage_wars_player_floor_height_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "funcs.h"
#include "floor_height.hpp"

#include <cstdint>
#include <cstring>

namespace {
using rage_wars::player::FloorObjectLookup;
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

    void store_word(ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        const auto address = argument(static_cast<std::uint32_t>(object + offset));
        std::memcpy(xr64_recomp_resolve_address(memory_, address), &value, sizeof(value));
    }

private:
    std::uint8_t* memory_;
};

class GeneratedFloorLookup final : public FloorObjectLookup {
public:
    GeneratedFloorLookup(std::uint8_t* memory, recomp_context& context)
        : memory_(memory), context_(context) {}

    ObjectId find_floor(ObjectId query_object) override {
        context_.r4 = argument(query_object);
        trace_func_0024E7BC_r0004F3BC(memory_, &context_);
        return static_cast<ObjectId>(context_.r2);
    }

private:
    std::uint8_t* memory_;
    recomp_context& context_;
};

bool enabled() {
    static const bool choice = xr64::rage_wars::recomp::feature_explicit_on("XR64_NATIVE_PLAYER_FLOOR_HEIGHT");
    return choice;
}
}

extern "C" int xr64_player_floor_height_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || memory == nullptr || context == nullptr || !context->mips3_float_mode) return 0;
    const auto actor = static_cast<ObjectId>(context->r4);
    const auto query_object = static_cast<ObjectId>(context->r5);
    const auto stack = static_cast<ObjectId>(context->r29);
    if (actor == 0 || query_object == 0 || (actor & 3U) != 0 || (query_object & 3U) != 0
            || stack < 0x18U || (stack & 7U) != 0) return 0;

    RdramWords storage(memory);
    const auto frame = static_cast<ObjectId>(stack - 0x18U);
    // Reproduce 0022C054's o32 frame stores before the direct floor lookup.
    storage.store_word(frame, 0x10, static_cast<std::uint32_t>(context->r16));
    storage.store_word(frame, 0x14, static_cast<std::uint32_t>(context->r31));

    recomp_context work = *context;
    work.r29 = argument(frame);
    work.r16 = argument(actor);
    work.r4 = argument(query_object);
    GeneratedFloorLookup lookup(memory, work);
    const auto floor = rage_wars::player::update_floor_height(storage, lookup, actor, query_object);

    // The original epilogue restores s0/ra from its just-written frame and
    // returns to the entry stack pointer. Keep the floor pointer in v0.
    const auto restored_s0 = static_cast<std::int32_t>(storage.load_word(frame, 0x10));
    const auto restored_ra = static_cast<std::int32_t>(storage.load_word(frame, 0x14));
    context->r16 = static_cast<gpr>(restored_s0);
    context->r31 = static_cast<gpr>(restored_ra);
    context->r29 = argument(stack);
    context->r4 = argument(query_object);
    context->r2 = work.r2;
    if (floor != 0) context->r1 = argument(0x800C0000U);
    return 1;
}
