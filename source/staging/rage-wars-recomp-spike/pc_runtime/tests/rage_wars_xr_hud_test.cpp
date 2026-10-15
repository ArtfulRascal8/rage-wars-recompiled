#include "rage_wars_xr_hud.hpp"
#include "rage_wars_xr_hand_ray.hpp"
#include <cmath>
#include <cstdio>
#include <random>

using namespace xr64::rage_wars::recomp;
int main() {
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> f(-1.0F, 1.0F);
    const XrHudSettings s{true, 2.5F, 2.2F};
    double worst = 0;
    for (int i = 0; i < 2000; ++i) {
        XrEyeFrame e;
        e.fov[0] = -0.9F; e.fov[1] = 0.8F; e.fov[2] = 0.85F; e.fov[3] = -0.9F;
        float q[4] = {f(rng) * 0.3F, f(rng), f(rng) * 0.3F, 1.0F};
        float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        float h[4] = {f(rng) * 0.3F, f(rng), f(rng) * 0.3F, 1.0F};
        float m = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2] + h[3] * h[3]);
        for (int k = 0; k < 4; ++k) { e.orientation[k] = q[k] / n; e.head_orientation[k] = h[k] / m; }
        for (int k = 0; k < 3; ++k) { e.head_position[k] = f(rng); e.position[k] = e.head_position[k] + f(rng) * 0.03F; }
        const auto M = xr_hud_matrix(e, 320, 240, s);
        for (int j = 0; j < 8; ++j) {
            const float px = (f(rng) + 1) * 160, py = (f(rng) + 1) * 120;
            // Expected: panel point in tracking space, projected by the shared point projection.
            const auto local = xr_hud_rotate(e.head_orientation,
                {(px / 160.0F - 1.0F) * 1.1F, (1.0F - py / 120.0F) * 0.825F, -2.5F});
            const std::array<float, 3> p{e.head_position[0] + local[0], e.head_position[1] + local[1], e.head_position[2] + local[2]};
            const auto want = xr_project_tracked_point(p, e);
            const float got[4] = {M[0] * px + M[4] * py + M[12], M[1] * px + M[5] * py + M[13],
                                  M[2] * px + M[6] * py + M[14], M[3] * px + M[7] * py + M[15]};
            const float w[4] = {want.x, want.y, want.z, want.w};
            for (int k = 0; k < 4; ++k) worst = std::fmax(worst, std::fabs(double(got[k]) - w[k]) / (1.0 + std::fabs(w[k])));
        }
    }
    // Panel centre straight ahead of an identity head projects to the view centre at depth.
    XrEyeFrame e; e.fov[0] = -0.8F; e.fov[1] = 0.8F; e.fov[2] = 0.8F; e.fov[3] = -0.8F;
    const auto M = xr_hud_matrix(e, 320, 240, s);
    const float cx = M[0] * 160 + M[4] * 120 + M[12], cy = M[1] * 160 + M[5] * 120 + M[13], cw = M[3] * 160 + M[7] * 120 + M[15];
    const bool centred = std::fabs(cx / cw) < 1e-5 && std::fabs(cy / cw) < 1e-5 && std::fabs(cw - 2.5F) < 1e-5;
    const bool rect_rule = xr_hud_rectangle(10, 10, 60, 30, 320, 240) && !xr_hud_rectangle(0, 0, 320, 240, 320, 240);
    const bool crosshair_rule = xr_hud_is_crosshair(152, 112, 168, 128, 320, 240) &&   // small centred reticle
        !xr_hud_is_crosshair(80, 110, 240, 130, 320, 240) &&                            // wide centred text
        !xr_hud_is_crosshair(20, 200, 40, 220, 320, 240) &&                             // corner icon
        !xr_hud_is_crosshair(0, 0, 320, 240, 320, 240);                                 // full-screen fill
    if (worst > 1e-4 || !centred || !rect_rule || !crosshair_rule) { std::fprintf(stderr, "FAIL hud worst=%g centred=%d rect=%d\n", worst, centred, rect_rule); return 1; }
    std::printf("PASS xr hud: matrix matches point projection (worst rel %.2e), centred, fill rectangles excluded\n", worst);
    return 0;
}
