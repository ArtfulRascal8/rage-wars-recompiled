#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace xr64::rage_wars {

struct InputSegment {
    std::uint32_t frames;
    std::uint16_t buttons;
    std::int8_t stick_x;
    std::int8_t stick_y;
};

// Neutral -> START -> neutral -> A -> neutral. The official runtime input
// callback will consume this exact timeline once resident main is executable.
constexpr std::array<InputSegment, 5> kGate11InputScript{{
        {60, 0x0000, 0, 0},
        {1, 0x1000, 0, 0},
        {30, 0x0000, 0, 0},
        {1, 0x8000, 0, 0},
        {30, 0x0000, 0, 0},
}};

constexpr std::size_t input_frame_count() {
    std::size_t count = 0;
    for (const InputSegment &segment : kGate11InputScript) count += segment.frames;
    return count;
}

constexpr std::uint64_t input_script_hash() {
    std::uint64_t hash = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (const InputSegment &segment : kGate11InputScript) {
        for (std::uint32_t frame = 0; frame < segment.frames; ++frame) {
            const std::array<std::uint8_t, 4> bytes{{
                    static_cast<std::uint8_t>(segment.buttons),
                    static_cast<std::uint8_t>(segment.buttons >> 8U),
                    static_cast<std::uint8_t>(segment.stick_x),
                    static_cast<std::uint8_t>(segment.stick_y),
            }};
            for (std::uint8_t byte : bytes) {
                hash ^= byte;
                hash *= prime;
            }
        }
    }
    return hash;
}

static_assert(input_frame_count() == 122);
static_assert(input_script_hash() == 0x41A02FDE0109D815ULL);

}  // namespace xr64::rage_wars
