#pragma once

#include "rage_wars_openxr.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace xr64::rage_wars::recomp {

// Head-locked VR HUD panel. The game draws its HUD in guest screen pixels;
// in XR gameplay those pixels are placed on a flat panel in front of the head
// so both eyes see one HUD (and one crosshair) at a real, readable depth.
struct XrHudSettings {
    bool enabled = true; float distance = 2.5F; float width = 2.2F;
    float menu_distance = 2.0F, menu_width = 2.0F;
};
// Current values: the VR "HUD & MENU" options, refreshed once per XR frame.
inline XrHudSettings& xr_hud_state() { static XrHudSettings s; return s; }
inline XrHudSettings xr_hud_settings() { return xr_hud_state(); }
// Launcher/test environment values override the saved options when present.
inline void xr_hud_configure(float distance, float width, float menu_distance, float menu_width) {
    auto& s = xr_hud_state();
    s.distance = distance; s.width = width; s.menu_distance = menu_distance; s.menu_width = menu_width;
    if (const char* v = std::getenv("XR64_XR_HUD_PANEL")) s.enabled = std::strcmp(v, "0") != 0;
    if (const char* v = std::getenv("XR64_XR_HUD_DISTANCE")) {
        const float d = std::strtof(v, nullptr); if (std::isfinite(d) && d >= 0.5F && d <= 20.0F) s.distance = d; }
    if (const char* v = std::getenv("XR64_XR_HUD_WIDTH")) {
        const float w = std::strtof(v, nullptr); if (std::isfinite(w) && w >= 0.2F && w <= 20.0F) s.width = w; }
}

inline std::array<float, 3> xr_hud_rotate(const float q[4], const std::array<float, 3>& v) {
    // v' = v + w*t + q x t, t = 2 q x v (unit quaternion q = x,y,z,w)
    const float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    return {v[0] + q[3] * tx + q[1] * tz - q[2] * ty, v[1] + q[3] * ty + q[2] * tx - q[0] * tz, v[2] + q[3] * tz + q[0] * ty - q[1] * tx};
}

// Eye clip coordinates of a tracking-space point (metres): the same math as
// xr_project_tracked_point (units 1, near 0.02, far 1000).
inline std::array<float, 4> xr_hud_eye_clip(const XrEyeFrame& eye, const std::array<float, 3>& p, bool translate = true) {
    const float l = std::tan(eye.fov[0]), r = std::tan(eye.fov[1]), u = std::tan(eye.fov[2]), d = std::tan(eye.fov[3]);
    const float inverse[4] = {-eye.orientation[0], -eye.orientation[1], -eye.orientation[2], eye.orientation[3]};
    const auto v = xr_hud_rotate(inverse, translate ? std::array<float, 3>{p[0] - eye.position[0], p[1] - eye.position[1], p[2] - eye.position[2]} : p);
    const float n = 0.02F, f = 1000.0F;
    return {2 * v[0] / (r - l) + (r + l) * v[2] / (r - l), 2 * v[1] / (u - d) + (u + d) * v[2] / (u - d),
            translate ? -(f + n) * v[2] / (f - n) - 2 * f * n / (f - n) : -(f + n) * v[2] / (f - n), -v[2]};
}

// Column-major matrix taking guest pixels (x, y, z, 1) to eye clip space via
// the head-locked panel. Pixel z is ignored: the panel is flat.
inline std::array<float, 16> xr_hud_matrix(const XrEyeFrame& eye, int logical_width, int logical_height,
        const XrHudSettings& s = xr_hud_settings()) {
    const float half_w = s.width * 0.5F, half_h = s.width * 0.375F; // 4:3 panel
    const float ax = 2.0F / float(logical_width) * half_w, ay = -2.0F / float(logical_height) * half_h;
    // Panel point for pixel (px, py): head + R_head * (ax*px - half_w, ay*py + half_h, -distance).
    const auto origin_head = xr_hud_rotate(eye.head_orientation, {-half_w, half_h, -s.distance});
    const std::array<float, 3> origin{eye.head_position[0] + origin_head[0], eye.head_position[1] + origin_head[1],
                                      eye.head_position[2] + origin_head[2]};
    const auto dx = xr_hud_rotate(eye.head_orientation, {ax, 0, 0});
    const auto dy = xr_hud_rotate(eye.head_orientation, {0, ay, 0});
    const auto c0 = xr_hud_eye_clip(eye, dx, false), c1 = xr_hud_eye_clip(eye, dy, false), c3 = xr_hud_eye_clip(eye, origin, true);
    return {c0[0], c0[1], c0[2], c0[3], c1[0], c1[1], c1[2], c1[3], 0, 0, 0, 0, c3[0], c3[1], c3[2], c3[3]};
}

// The game's own crosshair is a small HUD element centred on the screen. In
// VR with motion controls it is replaced by the controller reticle.
inline bool xr_hide_game_crosshair_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("XR64_XR_HIDE_GAME_CROSSHAIR");
        return !(v && std::strcmp(v, "0") == 0);
    }();
    return enabled;
}
inline bool xr_hud_is_crosshair(float left, float top, float right, float bottom, int width, int height) {
    const float w = right - left, h = bottom - top;
    if (!(w > 0) || !(h > 0) || w > 0.12F * width || h > 0.12F * height) return false;
    const float cx = (left + right) * 0.5F - width * 0.5F, cy = (top + bottom) * 0.5F - height * 0.5F;
    return std::fabs(cx) <= 0.04F * width && std::fabs(cy) <= 0.04F * height;
}

// HUD rectangles that cover nearly the whole screen are fills/clears, not HUD.
inline bool xr_hud_rectangle(float left, float top, float right, float bottom, int width, int height) {
    return (right - left) * (bottom - top) < 0.9F * float(width) * float(height);
}

} // namespace xr64::rage_wars::recomp
