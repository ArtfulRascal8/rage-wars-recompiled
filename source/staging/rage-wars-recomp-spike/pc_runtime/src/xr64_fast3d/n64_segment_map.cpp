#include "n64_segment_map.hpp"

namespace xr64 {

const std::vector<uint8_t> *N64SegmentMap::get(uint8_t segment_id) const {
    if (segment_id >= loaded.size() || !loaded[segment_id]) {
        return nullptr;
    }
    return &bytes[segment_id];
}

} // namespace xr64
