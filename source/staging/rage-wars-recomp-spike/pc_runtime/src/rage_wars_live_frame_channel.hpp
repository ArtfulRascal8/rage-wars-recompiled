#pragma once

#include <cstddef>
#include <cstdint>

namespace xr64::rage_wars::live_frame {

inline constexpr wchar_t kMappingName[] = L"Local\\XR64.RageWars.LiveFrame.v1";
inline constexpr std::uint64_t kMagic = 0x31464C5752343658ULL;
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kMaximumEventBytes = 16U * 1024U * 1024U;

// A single writer makes the generation odd while updating and even when the
// metadata and binary scene payload are stable for the Godot reader.
struct alignas(64) Header {
    std::uint64_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t header_size = 0;
    std::uint32_t payload_capacity = 0;
    // RW069 diagnostic transport fields. These reuse the pre-existing
    // reserved slots so the mapping size and offsets remain unchanged.
    std::uint32_t vertex_count = 0;
    alignas(8) std::uint64_t publication_generation = 0;
    std::uint64_t task_sequence = 0;
    std::uint32_t event_byte_count = 0;
    std::uint32_t surface_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t index_count = 0;
};

inline constexpr std::size_t kMappingBytes =
        sizeof(Header) + static_cast<std::size_t>(kMaximumEventBytes);

inline std::uint8_t *payload(Header *header) {
    return reinterpret_cast<std::uint8_t *>(header) + sizeof(Header);
}

inline const std::uint8_t *payload(const Header *header) {
    return reinterpret_cast<const std::uint8_t *>(header) + sizeof(Header);
}

} // namespace xr64::rage_wars::live_frame