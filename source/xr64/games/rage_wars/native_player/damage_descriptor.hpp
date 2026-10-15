#pragma once

#include "actor_storage.hpp"

#include <array>

namespace rage_wars::player {

using DamageDescriptorWords = std::array<std::uint32_t, 5>;

// Store the five words used by the Player_Tick damage payload constructor.
void write_damage_descriptor(WordStorage& storage, ObjectId address,
                              const DamageDescriptorWords& words);

} // namespace rage_wars::player
