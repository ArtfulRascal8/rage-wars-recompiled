#pragma once
#include "xr64_fast3d/n64_raw_fast3d_renderer.hpp"
#include <cstddef>
#include <cstdint>
namespace xr64::rage_wars::recomp {
inline bool startup_state(const std::uint8_t* rdram, std::size_t size) {
    constexpr std::size_t flag_offset = 0x140225U ^ 3U;
    return rdram && size > flag_offset && rdram[flag_offset] != 1;
}
// Only replace the untextured full-screen white fill used between startup cards.
// Artwork, menu details and gameplay effects keep their original colors.
inline bool startup_white_fill(bool startup, float left, float top, float right, float bottom,
        int width, int height, const ::xr64::N64RawFast3DDrawState& state) {
    return startup && !state.textured && width > 0 && height > 0 &&
        left <= 1 && top <= 1 && right >= width - 1 && bottom >= height - 1 &&
        state.fill_color.r >= 240 && state.fill_color.g >= 240 && state.fill_color.b >= 240;
}
inline auto startup_rectangle_fill(bool menu_panel, bool startup,
        float left, float top, float right, float bottom, int width, int height,
        const ::xr64::N64RawFast3DDrawState& state) {
    auto color = state.fill_color;
    if (menu_panel && startup_white_fill(startup, left, top, right, bottom, width, height, state))
        color.r = color.g = color.b = 0;
    return color;
}
}
