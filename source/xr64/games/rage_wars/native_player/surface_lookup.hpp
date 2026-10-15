#pragma once

#include "actor_storage.hpp"

namespace rage_wars::player {

class SurfaceTableLookup {
public:
    virtual ~SurfaceTableLookup() = default;
    [[nodiscard]] virtual ObjectId query_table(ObjectId table, std::uint32_t selector) = 0;
};

struct SurfaceLookupResult {
    std::uint32_t selector;
    ObjectId surface;
    bool queried_table;
};

// Original 0024E7BC behavior: a nonzero object+0x14 selects the fixed table
// at 0x80119DC8 and returns the result of its generated lookup helper.
[[nodiscard]] SurfaceLookupResult surface_lookup(WordStorage& storage,
                                                  SurfaceTableLookup& lookup,
                                                  ObjectId object);

} // namespace rage_wars::player
