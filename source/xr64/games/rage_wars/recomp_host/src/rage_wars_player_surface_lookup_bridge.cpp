#include "rage_wars_player_surface_lookup_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "funcs.h"
#include "surface_lookup.hpp"

#include <cstdint>
#include <cstring>

namespace {
using rage_wars::player::ObjectId;
using rage_wars::player::SurfaceLookupResult;
using rage_wars::player::SurfaceTableLookup;
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

class GeneratedSurfaceTableLookup final : public SurfaceTableLookup {
public:
    GeneratedSurfaceTableLookup(std::uint8_t* memory, recomp_context& context)
        : memory_(memory), context_(context) {}

    ObjectId query_table(ObjectId table, std::uint32_t selector) override {
        context_.r4 = argument(table);
        context_.r5 = static_cast<gpr>(static_cast<std::int32_t>(selector));
        context_.r2 = 0;
        resident_wave14_func_0028B248(memory_, &context_);
        raw_result_ = context_.r2;
        return static_cast<ObjectId>(context_.r2);
    }
    [[nodiscard]] gpr raw_result() const { return raw_result_; }

private:
    std::uint8_t* memory_;
    recomp_context& context_;
    gpr raw_result_ = 0;
};

bool enabled() {
    static const bool choice = xr64::rage_wars::recomp::feature_explicit_on("XR64_NATIVE_PLAYER_SURFACE_LOOKUP");
    return choice;
}
}

extern "C" int xr64_player_surface_lookup_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || memory == nullptr || context == nullptr) return 0;
    const auto object = static_cast<ObjectId>(context->r4);
    const auto stack = static_cast<ObjectId>(context->r29);
    if (object == 0 || (object & 3U) != 0 || stack < 0x18U || (stack & 7U) != 0) return 0;

    RdramWords storage(memory);
    const auto frame = static_cast<ObjectId>(stack - 0x18U);
    storage.store_word(frame, 0x10, static_cast<std::uint32_t>(context->r31));

    recomp_context work = *context;
    work.r29 = argument(frame);
    work.r2 = 0;
    GeneratedSurfaceTableLookup lookup(memory, work);
    const SurfaceLookupResult result = rage_wars::player::surface_lookup(storage, lookup, object);

    const auto restored_ra = static_cast<std::int32_t>(storage.load_word(frame, 0x10));
    context->r29 = argument(stack);
    context->r31 = static_cast<gpr>(restored_ra);
    context->r5 = static_cast<gpr>(static_cast<std::int32_t>(result.selector));
    context->r2 = result.queried_table ? lookup.raw_result() : 0;
    context->r4 = result.queried_table ? argument(0x80119DC8U) : context->r4;
    return 1;
}
