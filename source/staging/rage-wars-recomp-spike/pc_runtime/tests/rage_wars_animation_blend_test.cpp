#include "rage_wars_animation_blend.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using namespace xr64;
using namespace xr64::rage_wars::recomp;
using Clock = AnimationBlend::Clock;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}
bool near(float a, float b, float tolerance = 1e-3F) { return std::abs(a - b) <= tolerance; }

std::array<float, 16> identity() {
    std::array<float, 16> m{};
    m[0] = m[5] = m[10] = m[15] = 1.0F;
    return m;
}
// Column-major perspective-like basis with a camera translation.
std::array<float, 16> perspective(float camera_x) {
    std::array<float, 16> m{};
    m[0] = 1.5F; m[5] = 2.0F; m[10] = -1.01F; m[11] = -1.0F; m[14] = -2.0F;
    m[12] = -1.5F * camera_x;
    return m;
}
N64RawFast3DVertex clip(const std::array<float, 16>& m, float x, float y, float z, float s) {
    N64RawFast3DVertex v;
    const float p[4]{x, y, z, 1.0F};
    float c[4]{};
    for (int r = 0; r < 4; ++r) for (int k = 0; k < 4; ++k) c[r] += m[k * 4 + r] * p[k];
    v.x = c[0]; v.y = c[1]; v.z = c[2]; v.w = c[3]; v.s = s; v.t = s * 0.5F;
    return v;
}
struct Mesh { float dx = 0, dy = 0, dz = 0; float s0 = 0; std::size_t count = 3; };
struct Scene {
    std::array<float, 16> projection = identity();
    std::vector<Mesh> meshes;
    std::uint8_t texture_byte = 1;
    bool camera_attachment = false;
};
std::shared_ptr<const N64DecodedFrame> build(const Scene& scene) {
    auto frame = std::make_shared<N64DecodedFrame>();
    std::string error;
    frame->begin_frame(320, 240, error);
    N64RawFast3DTexture texture;
    texture.rgba = {scene.texture_byte, 2, 3, 4};
    frame->upload_texture(texture, error);
    for (const auto& mesh : scene.meshes) {
        N64RawFast3DDrawState state;
        state.source_projection = scene.projection;
        state.textured = true;
        state.camera_attachment = scene.camera_attachment;
        std::vector<N64RawFast3DVertex> vertices;
        for (std::size_t i = 0; i < mesh.count; ++i)
            vertices.push_back(clip(scene.projection, float(i) + mesh.dx, 1.0F + mesh.dy, -5.0F + mesh.dz, mesh.s0 + float(i)));
        frame->draw_triangles(vertices, state, error);
    }
    frame->end_frame(error);
    return frame;
}
const Clock::time_point T0 = Clock::now();
Clock::time_point at(double ms) { return T0 + std::chrono::nanoseconds(static_cast<std::int64_t>(ms * 1e6)); }
constexpr std::int64_t delay = 40'000'000;

// Observe a then b 33 ms apart; prepare so that target time is `fraction` of the way.
const N64RawFast3DVertex* run(AnimationBlend& blend, const std::shared_ptr<const N64DecodedFrame>& a,
        const std::shared_ptr<const N64DecodedFrame>& b, double fraction, std::size_t batch = 0,
        std::uint64_t gen_b = 2, double gap_ms = 33.0, bool eligible = true, std::int64_t d = delay) {
    blend.reset();
    blend.observe(a, 1, at(0), true);
    blend.observe(b, gen_b, at(gap_ms), eligible);
    blend.prepare(at(fraction * gap_ms + 40.0), d);
    const auto views = b->triangle_batch_views();
    return batch < views.size() ? blend.find(views[batch].vertices, views[batch].vertex_count) : nullptr;
}
}

int main() {
    AnimationBlend blend;
    // 1. Halfway blend in an identity basis.
    {
        Scene a; a.meshes = {Mesh{}};
        Scene b; b.meshes = {Mesh{10, 0, 0}};
        const auto fa = build(a), fb = build(b);
        const auto* v = run(blend, fa, fb, 0.5);
        check(v && near(v[0].x, 5.0F) && near(v[2].x, 7.0F), "identity halfway blend");
        check(blend.matched_batches() == 1 && blend.blended_vertices() == 3, "match counters");
        const auto* start = run(blend, fa, fb, -0.5);
        check(start && near(start[0].x, 0.0F), "target before previous clamps to previous pose");
        check(!run(blend, fa, fb, 1.5), "target after current draws current scene");
        check(!run(blend, fa, fb, 0.5, 0, 2, 33.0, true, 0), "zero delay disables");
        const auto* skipped = run(blend, fa, fb, 0.5, 0, 3);
        check(skipped && near(skipped[0].x, 5.0F), "skipped generation with small gap keeps blending");
        check(!run(blend, fa, fb, 0.5, 0, 2, 150.0), "stale interval disables");
        check(!run(blend, fa, fb, 0.5, 0, 2, 33.0, false), "menu or startup scene disables");
    }
    // 2. Perspective basis: blended world point re-expressed in the current basis.
    {
        Scene a; a.projection = perspective(0); a.meshes = {Mesh{}};
        Scene b; b.projection = perspective(2); b.meshes = {Mesh{4, 2, 0}};
        const auto fb = build(b);
        const auto* v = run(blend, build(a), fb, 0.25);
        const auto expected = clip(b.projection, 0.0F + 1.0F, 1.0F + 0.5F, -5.0F, 0.0F);
        check(v && near(v[0].x, expected.x) && near(v[0].y, expected.y) && near(v[0].w, expected.w),
              "perspective quarter blend in current basis");
    }
    // 3. Camera moved, geometry static: nothing to blend.
    {
        Scene a; a.projection = perspective(0); a.meshes = {Mesh{}};
        Scene b; b.projection = perspective(3); b.meshes = {Mesh{}};
        check(!run(blend, build(a), build(b), 0.5), "static world under moving camera is untouched");
    }
    // 4. Mismatches are drawn unblended.
    {
        Scene a; a.meshes = {Mesh{}};
        Scene st; st.meshes = {Mesh{10, 0, 0, 7}};
        check(!run(blend, build(a), build(st), 0.5), "different texture coordinates rejected");
        Scene count; count.meshes = {Mesh{10, 0, 0, 0, 6}};
        check(!run(blend, build(a), build(count), 0.5), "different vertex count rejected");
        Scene tex; tex.meshes = {Mesh{10}}; tex.texture_byte = 9;
        check(!run(blend, build(a), build(tex), 0.5), "different texture rejected");
        Scene jump; jump.meshes = {Mesh{150}};
        check(!run(blend, build(a), build(jump), 0.5), "teleport distance rejected");
        Scene attached; attached.meshes = {Mesh{10}}; attached.camera_attachment = true;
        check(!run(blend, build(a), build(attached), 0.5), "attachment kind must agree");
    }
    // 5. An inserted batch (effect) before the matched mesh keeps pairing.
    {
        Scene a; a.meshes = {Mesh{0, 0, 0, 0}, Mesh{0, 0, 0, 20}};
        Scene b; b.meshes = {Mesh{0, 0, 0, 50, 6}, Mesh{10, 0, 0, 0}, Mesh{4, 0, 0, 20}};
        const auto fb = build(b);
        const auto* first = run(blend, build(a), fb, 0.5, 1);
        const auto views = fb->triangle_batch_views();
        const auto* second = blend.find(views[2].vertices, views[2].vertex_count);
        check(!blend.find(views[0].vertices, views[0].vertex_count), "inserted batch draws as is");
        check(first && near(first[0].x, 5.0F) && second && near(second[0].x, 2.0F), "pairing survives insertion");
        check(!blend.find(views[1].vertices, 2), "lookup requires matching vertex count");
    }
    // 6. Identical meshes that swap draw order pair with their own previous pose.
    {
        Scene a; a.meshes = {Mesh{0, 0, 0, 3}, Mesh{50, 0, 0, 3}};
        Scene b; b.meshes = {Mesh{51, 0, 0, 3}, Mesh{1, 0, 0, 3}};
        const auto fb = build(b);
        const auto* first = run(blend, build(a), fb, 0.5, 0);
        const auto views = fb->triangle_batch_views();
        const auto* second = blend.find(views[1].vertices, views[1].vertex_count);
        check(first && near(first[0].x, 50.5F), "swapped look-alike blends from its own pose");
        check(second && near(second[0].x, 0.5F), "second look-alike blends from its own pose");
    }
    // 7. Camera-attached mesh (first-person weapon): it moves with the camera,
    // so its clip coordinates stay put while its world position jumps.
    {
        Scene a; a.projection = perspective(0); a.meshes = {Mesh{0, 0, 0, 9}};
        Scene b; b.projection = perspective(5); b.meshes = {Mesh{5, 0, 0, 9}};   // follows camera x
        check(!run(blend, build(a), build(b), 0.5), "weapon fixed to moving camera is not world-blended");
        Scene c; c.projection = perspective(5); c.meshes = {Mesh{5, 0.4F, 0, 9}}; // plus a small bob
        const auto fc = build(c);
        const auto* v = run(blend, build(a), fc, 0.5);
        const auto expected = clip(c.projection, 5.0F, 1.2F, -5.0F, 9.0F);    // half the bob, camera-relative
        check(v && near(v[0].x, expected.x) && near(v[0].y, expected.y) && near(v[0].w, expected.w),
              "weapon bob blends camera-relative");
    }
    // 8. Cost: a large animated scene (informational, plus a generous bound).
    {
        Scene a, b; a.projection = b.projection = perspective(0);
        for (int i = 0; i < 200; ++i) { a.meshes.push_back(Mesh{0, 0, 0, float(i * 1000), 300}); b.meshes.push_back(Mesh{1, 0.5F, 0, float(i * 1000), 300}); }
        const auto fa = build(a), fb = build(b);
        blend.reset();
        const auto r0 = Clock::now();
        blend.observe(fa, 1, at(0), true); blend.observe(fb, 2, at(33), true);
        const double rebuild_us = std::chrono::duration<double, std::micro>(Clock::now() - r0).count();
        const int rounds = 200;
        const auto p0 = Clock::now();
        for (int i = 0; i < rounds; ++i) blend.prepare(at(16.5 + 40.0 + i * 0.01), delay);
        const double prepare_us = std::chrono::duration<double, std::micro>(Clock::now() - p0).count() / rounds;
        std::printf("COST vertices=%zu rebuild_us=%.1f prepare_us_per_frame=%.1f\n", blend.blended_vertices(), rebuild_us, prepare_us);
        check(blend.blended_vertices() == 60000, "large scene fully matched");
        check(prepare_us < 4000.0, "per-frame blend cost bounded");
    }
    if (failures) { std::fprintf(stderr, "%d animation blend checks failed\n", failures); return 1; }
    std::puts("PASS animation blend: halfway/perspective blending, static-camera skip, mismatch and teleport rejection, timing guards, insertion-tolerant pairing, cost");
    return 0;
}
