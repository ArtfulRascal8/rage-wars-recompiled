#include "rage_wars_player_ground_contact_bridge.h"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "ground_contact.hpp"

#include <cstdint>
#include <cstring>

namespace {
using rage_wars::player::GroundContactResult;
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

    std::uint8_t load_byte(ObjectId object, std::uint32_t offset) const override {
        const auto address = static_cast<gpr>(static_cast<std::uint32_t>(object + offset) ^ 3U);
        return *xr64_recomp_resolve_address(memory_, address);
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
    static const bool choice = xr64::rage_wars::recomp::feature_explicit_on("XR64_NATIVE_PLAYER_GROUND_CONTACT");
    return choice;
}
}

extern "C" int xr64_player_ground_contact_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || memory == nullptr || context == nullptr) return 0;
    const auto object = static_cast<ObjectId>(context->r4);
    if (object == 0 || (object & 3U) != 0) return 0;

    GroundContactResult result{};
    {
        PreserveContext restore{*context, *context};
        RdramWords words(memory);
        result = rage_wars::player::ground_contact(words, object);
    }
    // The original leaf leaves v1 as the loaded type byte and returns its
    // contact code in v0. Preserve every other guest register.
    context->r3 = static_cast<gpr>(result.object_type);
    context->r2 = static_cast<gpr>(result.contact_code);
    return 1;
}
