#pragma once

#include "actor_storage.hpp"

namespace rage_wars::player {

// Original 00274860 behavior: dt-scaled interpolation toward a target, with
// directional clamping and the original snap-rate special case.
void ease_toward(WordStorage& storage, ObjectId address, float target, float rate,
                 float snap_rate, float frame_time);

} // namespace rage_wars::player
