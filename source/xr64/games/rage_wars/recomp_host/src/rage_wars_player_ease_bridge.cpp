#include "rage_wars_player_ease_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "ease_toward.hpp"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {
using rage_wars::player::ObjectId;
using rage_wars::player::RageWarsActorStorage;
using rage_wars::player::WordStorage;

class RdramWords final : public WordStorage {
public:
    explicit RdramWords(std::uint8_t* memory) : memory_(memory) {}

    std::uint32_t load_word(ObjectId object, std::uint32_t offset) const override {
        std::uint32_t value{};
        const auto address = static_cast<gpr>(static_cast<std::uint32_t>(object + offset));
        std::memcpy(&value, xr64_recomp_resolve_address(memory_, address), sizeof(value));
        return value;
    }

    void store_word(ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        const auto address = static_cast<gpr>(static_cast<std::uint32_t>(object + offset));
        std::memcpy(xr64_recomp_resolve_address(memory_, address), &value, sizeof(value));
    }

private:
    std::uint8_t* memory_;
};

struct PreserveContext {
    recomp_context& context;
    recomp_context saved;
    ~PreserveContext() { context = saved; }
};

bool enabled() {
    static const bool choice = xr64::rage_wars::recomp::feature_explicit_on("XR64_NATIVE_PLAYER_EASE");
    return choice;
}
}

extern "C" int xr64_player_ease_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || memory == nullptr || context == nullptr || !context->mips3_float_mode) return 0;
    PreserveContext restore{*context, *context};
    RdramWords words(memory);
    RageWarsActorStorage storage(words);
    const auto address = static_cast<ObjectId>(context->r4);
    const float target = std::bit_cast<float>(static_cast<std::uint32_t>(context->r5));
    const float rate = std::bit_cast<float>(static_cast<std::uint32_t>(context->r6));
    const float snap_rate = std::bit_cast<float>(words.load_word(0x800C4894U, 0));
    const float frame_time = std::bit_cast<float>(words.load_word(0x800CD648U, 0));
    rage_wars::player::ease_toward(words, address, target, rate, snap_rate, frame_time);
    return 1;
}
