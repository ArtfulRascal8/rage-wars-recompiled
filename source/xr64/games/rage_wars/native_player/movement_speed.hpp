#pragma once

#include "actor_storage.hpp"

namespace rage_wars::player {

// Player_Tick dependency 002228E4. Arguments are the actor, its movement
// source record and an optional per-state speed record.
void update_movement_speed(WordStorage& storage, ObjectId actor,
                           ObjectId movement_source, ObjectId state_record);

} // namespace rage_wars::player
