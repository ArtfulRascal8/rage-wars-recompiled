#include "surface_lookup.hpp"

namespace rage_wars::player {

SurfaceLookupResult surface_lookup(WordStorage& storage, SurfaceTableLookup& lookup,
                                   ObjectId object) {
    const auto selector = storage.load_word(object, 0x14);
    if (selector == 0) return {selector, 0, false};
    return {selector, lookup.query_table(0x80119DC8U, selector), true};
}

} // namespace rage_wars::player
