#pragma once

#include "actor_storage.hpp"

namespace rage_wars::player {

struct GroundContactResult {
    std::uint32_t object_type;
    std::uint32_t contact_code;
};

// Original 0024E60C behavior: only type-1 objects expose the low three bits
// of their word at +0x38 as a ground-contact code.
[[nodiscard]] GroundContactResult ground_contact(const WordStorage& storage, ObjectId object);

} // namespace rage_wars::player
