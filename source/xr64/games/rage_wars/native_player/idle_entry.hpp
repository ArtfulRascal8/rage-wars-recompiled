#pragma once

#include "actor_storage.hpp"

namespace rage_wars::player {

class PlayerStateTransition {
public:
    virtual ~PlayerStateTransition() = default;
    virtual void set_state(ObjectId actor, std::uint32_t caller_context,
                           std::uint32_t state) = 0;
};

// State-2 entry callback 0022C878: when either planar movement component is
// nonzero, immediately transition the actor to state 3.
void enter_idle_state(WordStorage& storage, PlayerStateTransition& transition,
                      ObjectId actor, std::uint32_t caller_context);

} // namespace rage_wars::player
