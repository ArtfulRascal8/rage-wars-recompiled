#pragma once

#include "rage_wars_animation_blend.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace xr64::rage_wars::recomp {

// VR aim laser: the right-hand shot ray (same ray the shot hook supplies to the
// native spawner) intersected with the decoded frame's world triangles, so the
// laser and its dot stop on walls, floors and characters.
inline float xr_laser_units() {
    static const float units = [] {
        float u = 100.0F;
        if (const char* v = std::getenv("XR64_XR_UNITS_PER_METRE")) {
            const float p = std::strtof(v, nullptr); if (std::isfinite(p) && p > 0) u = p; }
        return u;
    }();
    return units;
}

// World-space triangles of one decoded generation. HUD, first-person
// (camera/tracked attachment) and alpha-blended effect draws are excluded.
struct XrLaserWorld {
    std::uint64_t generation = ~0ULL;
    std::vector<std::array<float, 9>> triangles;

    void build(const N64DecodedFrame& frame, std::uint64_t gen) {
        if (gen == generation) return;
        generation = gen;
        triangles.clear();
        for (const auto& view : frame.triangle_batch_views()) {
            const auto& s = *view.state;
            if (!s.perspective_projection || s.camera_attachment || s.tracked_attachment || s.alpha_blend ||
                    view.vertex_count < 3) continue;
            std::array<double, 16> inverse{};
            if (!animation_invert4(s.source_projection, inverse)) continue;
            for (std::size_t i = 0; i + 2 < view.vertex_count; i += 3) {
                std::array<float, 3> a{}, b{}, c{};
                if (!animation_unproject(inverse, view.vertices[i], a) ||
                        !animation_unproject(inverse, view.vertices[i + 1], b) ||
                        !animation_unproject(inverse, view.vertices[i + 2], c)) continue;
                triangles.push_back({a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]});
            }
        }
    }

    // Nearest double-sided hit along a unit direction in (t_min, t_max).
    bool raycast(const std::array<float, 3>& o, const std::array<float, 3>& d, float t_min, float t_max, float& t_hit) const {
        bool hit = false;
        float best = t_max;
        for (const auto& tri : triangles) {
            const float e1[3] = {tri[3] - tri[0], tri[4] - tri[1], tri[5] - tri[2]};
            const float e2[3] = {tri[6] - tri[0], tri[7] - tri[1], tri[8] - tri[2]};
            const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
            const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
            if (std::fabs(det) < 1e-9F) continue;
            const float inv = 1.0F / det;
            const float s[3] = {o[0] - tri[0], o[1] - tri[1], o[2] - tri[2]};
            const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
            if (u < 0.0F || u > 1.0F) continue;
            const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
            const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
            if (v < 0.0F || u + v > 1.0F) continue;
            const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
            if (t > t_min && t < best) { best = t; hit = true; }
        }
        if (hit) t_hit = best;
        return hit;
    }
};

} // namespace xr64::rage_wars::recomp
