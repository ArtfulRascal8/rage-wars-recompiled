#pragma once

#include "actor_storage.hpp"

namespace rage_wars::player {

class FloorObjectLookup {
public:
    virtual ~FloorObjectLookup() = default;
    [[nodiscard]] virtual ObjectId find_floor(ObjectId query_object) = 0;
};

// Original 0022C054 behavior: if the lookup returns a floor object, blend the
// actor's +0x780 toward floor+0x2C by the fixed factor at 0x800C2C58.
[[nodiscard]] ObjectId update_floor_height(WordStorage& storage, FloorObjectLookup& lookup,
                                           ObjectId actor, ObjectId query_object);

} // namespace rage_wars::player
