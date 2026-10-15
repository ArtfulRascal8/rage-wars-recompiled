#include "rage_wars_player_damage_descriptor_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "damage_descriptor.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {
using rage_wars::player::DamageDescriptorWords;
using rage_wars::player::ObjectId;
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
    static const bool choice = xr64::rage_wars::recomp::feature_explicit_on("XR64_NATIVE_PLAYER_DAMAGE_DESCRIPTOR");
    return choice;
}
}

extern "C" int xr64_player_damage_descriptor_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || memory == nullptr || context == nullptr) return 0;
    const auto address = static_cast<ObjectId>(context->r4);
    const auto stack = static_cast<ObjectId>(context->r29);
    if (address == 0 || (address & 3U) != 0 || (stack & 3U) != 0) return 0;

    const auto fourth = static_cast<std::uint32_t>(
        RdramWords(memory).load_word(stack, 0x10));
    const auto fifth = static_cast<std::uint32_t>(
        RdramWords(memory).load_word(stack, 0x14));
    const DamageDescriptorWords words{
        static_cast<std::uint32_t>(context->r5),
        static_cast<std::uint32_t>(context->r6),
        static_cast<std::uint32_t>(context->r7),
        fourth,
        fifth,
    };
    {
        PreserveContext restore{*context, *context};
        RdramWords storage(memory);
        rage_wars::player::write_damage_descriptor(storage, address, words);
    }
    // 00216488 returns the two incoming stack arguments in v0/v1 while
    // preserving the other visible guest registers.
    context->r2 = static_cast<gpr>(static_cast<std::int32_t>(fourth));
    context->r3 = static_cast<gpr>(static_cast<std::int32_t>(fifth));
    return 1;
}
