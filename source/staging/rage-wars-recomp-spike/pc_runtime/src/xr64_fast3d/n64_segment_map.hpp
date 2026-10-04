#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace xr64 {

struct N64SegmentMap {
    std::array<std::vector<uint8_t>, 32> bytes;
    std::array<bool, 32> loaded{};
    // Live task decoding may retain the complete task-start RDRAM snapshot.
    // In that mode the decoder resolves each segmented address against this
    // evolving base table instead of treating the final base as static.
    std::vector<uint8_t> live_rdram;
    std::array<uint32_t, 32> live_segment_bases{};
    std::array<bool, 32> live_segment_loaded{};


    const std::vector<uint8_t> *get(uint8_t segment_id) const;
};

} // namespace xr64
