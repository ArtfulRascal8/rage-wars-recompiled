// Portable production-helper contracts. All publication schedules are fake;
// this never executes original game code, an SDL renderer, or a headset.
#include "rage_wars_body_interpolation.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace xr64::rage_wars::recomp;
constexpr std::int64_t second = 1'000'000'000;
constexpr std::int64_t start_time = second;
constexpr std::int64_t delay = 40'000'000;

static void require(bool condition, const char* reason) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); }
}
static bool close(float a, float b) { return std::abs(a - b) < 0.0001F; }
static LocalPlayerPresentation player(std::int64_t time, float x) {
    LocalPlayerPresentation p;
    p.valid = true; p.actor = 0x80200000; p.update = 1; p.continuity = 1;
    p.sampled_ns = time; p.origin = {x, 0, 0};
    p.projection = {2,0,0,0, 0,3,0,0, 0,0,1,0.5F, -2*x,0,0,0};
    return p;
}
static LocalPresentationState state(std::int64_t time, float x, std::uint64_t update) {
    LocalPresentationState s; s.player = player(time, x); s.player.update = update;
    return s;
}
static void high_refresh(int hz) {
    LocalPresentationMailbox box;
    box.mouse_context(true);
    xr64::rage_wars::controls::Settings settings;
    LocalBodyInterpolation interpolation;
    auto p = player(start_time, 0);
    LocalPlayerPresentation retained;
    int last_step = -1, publications = 0, distinct = 0, repeats = 0;
    float last_x = 0, last_yaw = -1;
    for (int frame = 0; frame < hz; ++frame) {
        const auto now = start_time + second * frame / hz;
        const int step = frame * 30 / hz;
        if (step != last_step) {
            const auto mouse = box.take_mouse();
            p.yaw += float(mouse.mouse_yaw);
            box.applied_mouse(mouse, false); box.applied_mouse(mouse, true);
            p.origin[0] = float(step); p.projection[12] = -2 * p.origin[0];
            p.sampled_ns = start_time + second * step / 30;
            box.publish(p); retained = box.acquire().player;
            last_step = step; ++publications;
        } else ++repeats;
        box.add_mouse(1, 0, settings);
        const auto current = box.acquire();
        const auto fresh = local_camera_correction(retained, current, now);
        const auto c = interpolation.camera(retained, current, now, delay);
        require(c.valid && c.yaw == fresh.yaw && c.pitch == fresh.pitch,
                "body delay preserves fresh angular displacement");
        const auto visual_yaw = c.yaw + retained.yaw;
        require(visual_yaw > last_yaw, "mouse reaches every display sample across new scenes");
        last_yaw = visual_yaw;
        const float expected = float(double((std::max)(std::int64_t(0), now-start_time-delay))*30/second);
        require(close(c.target[12], expected), "fixed delay follows completed linear endpoints at display cadence");
        require(current.player.origin == p.origin && current.player.sampled_ns == p.sampled_ns,
                "visual interpolation leaves authoritative publication unchanged");
        require(c.input_sequence == fresh.input_sequence && c.player_update == fresh.player_update,
                "visual interpolation preserves endpoint identities");
        if (!frame || c.target[12] != last_x) ++distinct;
        require(c.target[12] >= last_x, "new decoded generations cannot rewind the visual timeline");
        last_x = c.target[12];
    }
    require(publications == 30 && distinct > 30 && repeats == hz-30,
            "display samples improve body motion without extra publications");
    std::printf("PASS synthetic: display_hz=%d publications=%d decoded_generations=30 repeats=%d "
            "distinct_visual_origins=%d fresh_mouse_samples=%d body_delay_ms=40\n",
            hz, publications, repeats, distinct, hz);
}
static void timing_guards() {
    LocalBodyInterpolation interpolation;
    auto s = state(start_time, 0, 1); const auto scene = s.player;
    const auto baseline = local_camera_correction(scene, s, start_time);
    require(interpolation.camera(scene, s, start_time, 0).target == baseline.target,
            "disabled experiment exactly preserves the existing transform");
    require(interpolation.camera(scene, s, start_time, 101'000'000).target == baseline.target,
            "invalid delay disables interpolation");
    (void)interpolation.camera(scene, s, start_time, delay);
    s = state(start_time+20'000'000, 2, 2);
    (void)interpolation.camera(scene, s, s.player.sampled_ns, delay);
    s = state(start_time+60'000'000, 6, 3);
    auto c = interpolation.camera(scene, s, s.player.sampled_ns, delay);
    require(close(c.target[12], 2), "variable intervals use a fixed target time");
    c = interpolation.camera(scene, s, start_time+70'000'000, delay);
    require(close(c.target[12], 3), "bracketed variable interval interpolates elapsed time");
    c = interpolation.camera(scene, s, start_time+150'000'000, delay);
    require(close(c.target[12], 6), "no extrapolation beyond the newest completed sample");
    c = interpolation.camera(scene, s, start_time+60'000'000, delay);
    require(close(c.target[12], 6), "backward presentation time resets history");
    c = interpolation.camera(scene, s, start_time+310'000'001, delay);
    require(!c.valid, "stale player invalidates correction and history");
    s = state(start_time+320'000'000, 8, 4);
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 8),
            "resume cannot reuse stale interpolation endpoints");
    s = state(start_time+340'000'000, 10, 6);
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 10),
            "missed publication resets instead of inventing a collision path");
    s = state(start_time+340'000'000, 12, 7);
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 12),
            "equal completion timestamps reset history");
    s.player.origin[0] = 14;
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 14),
            "mutated duplicate publication resets history");
    s.player.origin[0] = std::numeric_limits<float>::quiet_NaN();
    require(!interpolation.camera(scene, s, s.player.sampled_ns, delay).valid,
            "nonfinite publication rejects experimental correction");
    s = state(start_time+360'000'000, 16, 8);
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 16),
            "nonfinite publication cannot survive in interpolation history");
    interpolation.reset();
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 16),
            "desktop/XR transition reset starts from a completed origin");
    std::puts("PASS timing: disabled, variable intervals, hold, rollback, stale, missed, duplicate and reset guards");
}
static void discontinuities() {
    LocalBodyInterpolation interpolation;
    auto s = state(start_time, 0, 1); auto scene = s.player;
    (void)interpolation.camera(scene, s, start_time, delay);
    s = state(start_time+20'000'000, 2, 2); s.mouse.epoch = 2;
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 2),
            "focus/input epoch starts a new body segment");
    s = state(start_time+40'000'000, 202, 3); s.player.continuity = 2;
    require(!interpolation.camera(scene, s, s.player.sampled_ns, delay).valid,
            "old decoded scene stays rejected after teleport");
    scene = s.player;
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 202),
            "new teleport scene cannot blend with prior actor position");
    s.player.actor += 0x100; s.player.update++;
    require(!interpolation.camera(scene, s, s.player.sampled_ns, delay).valid,
            "actor change rejects old scene");
    scene = s.player;
    (void)interpolation.camera(scene, s, s.player.sampled_ns, delay);
    s.player.valid = false;
    require(!interpolation.camera(scene, s, s.player.sampled_ns, delay).valid,
            "pause/death/menu invalidation rejects history");
    s.player.valid = true; s.player.update++; s.player.sampled_ns += 20'000'000;
    s.player.origin[0] = 204;
    require(close(interpolation.camera(scene, s, s.player.sampled_ns, delay).target[12], 204),
            "validity recovery starts from current origin");
    // Exercise the bounded history fallback without assuming a guest frequency.
    interpolation.reset();
    for (int i = 0; i < 40; ++i) {
        s.player.sampled_ns = start_time + i*1'000'000; s.player.update = i+1;
        s.player.origin[0] = float(i);
        const auto c = interpolation.camera(scene, s, s.player.sampled_ns, 100'000'000);
        require(c.valid && std::isfinite(c.target[12]) && c.target[12] <= i,
                "history exhaustion never predicts a future position");
    }
    std::puts("PASS discontinuities: focus, teleport, actor, validity, bounded history");
}
int main() {
    require(parse_local_body_delay_ns(nullptr) == 0 && parse_local_body_delay_ns("") == 0 &&
            parse_local_body_delay_ns("0") == 0 && parse_local_body_delay_ns("-1") == 0 &&
            parse_local_body_delay_ns("40junk") == 0 && parse_local_body_delay_ns("40.5") == 0 &&
            parse_local_body_delay_ns("101") == 0 && parse_local_body_delay_ns("999999999999") == 0 &&
            parse_local_body_delay_ns("1") == 1'000'000 && parse_local_body_delay_ns("100") == 100'000'000,
            "only explicit bounded integer delays enable the experiment");
    high_refresh(60); high_refresh(120); high_refresh(144);
    timing_guards(); discontinuities();
    std::puts("PASS portable helper contracts; native gameplay and headset acceptance remain untested.");
}
