#pragma once

#include "xr64_fast3d/n64_decoded_frame.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "rage_wars_feature_defaults.hpp"

namespace xr64::rage_wars::recomp {

// Presentation-only animation smoothing. The game finishes its animation work
// once per completed scene (about 30 Hz). Between scenes each matched triangle
// batch is drawn part way from its previous to its current position, using the
// same visual delay as the body interpolation. Guest work, collision, timing and
// the decoded payloads are unchanged. On by default; XR64_ANIMATION_BLEND=0 switches it off.
inline bool animation_blend_enabled() {
    static const bool enabled = [] {
        return feature_default_on("XR64_ANIMATION_BLEND");
    }();
    return enabled;
}
inline bool animation_blend_stats_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("XR64_ANIMATION_BLEND_STATS");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Column-major 4x4 (index j*4+i is column j, row i), as source_projection.
inline bool animation_invert4(const std::array<float, 16>& m, std::array<double, 16>& out) {
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) a[r][c] = m[c * 4 + r];
        for (int c = 0; c < 4; ++c) a[r][4 + c] = r == c ? 1.0 : 0.0;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        if (std::abs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col) for (int c = 0; c < 8; ++c) std::swap(a[pivot][c], a[col][c]);
        const double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            if (f != 0.0) for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out[c * 4 + r] = a[r][4 + c];
    return true;
}

// Clip-space vertex back to the batch's source space (world, or view space for
// camera-attached lists). Fails for points at or behind the projection plane.
inline bool animation_unproject(const std::array<double, 16>& inverse,
        const N64RawFast3DVertex& v, std::array<float, 3>& point) {
    const double clip[4]{v.x, v.y, v.z, v.w};
    double p[4]{};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) p[i] += inverse[j * 4 + i] * clip[j];
    if (!std::isfinite(p[3]) || std::abs(p[3]) < 1e-9) return false;
    for (int i = 0; i < 3; ++i) {
        point[i] = static_cast<float>(p[i] / p[3]);
        if (!std::isfinite(point[i])) return false;
    }
    return true;
}

// Two batches describe the same mesh when their placement, material, texture,
// vertex count and texture coordinates agree. Anything else is drawn unblended.
inline bool animation_batches_match(const N64DecodedFrame::TriangleBatchView& a,
        const N64DecodedFrame::TriangleBatchView& b) {
    if (a.vertex_count == 0 || a.vertex_count != b.vertex_count) return false;
    if (a.texture_hash != b.texture_hash) return false;
    const auto& s = *a.state;
    const auto& t = *b.state;
    if (s.tracked_attachment || t.tracked_attachment) return false;
    if (s.camera_attachment != t.camera_attachment || s.perspective_projection != t.perspective_projection ||
            s.textured != t.textured || s.alpha_blend != t.alpha_blend ||
            s.other_mode != t.other_mode || s.combine_word0 != t.combine_word0 ||
            s.combine_word1 != t.combine_word1 || s.geometry_mode != t.geometry_mode ||
            std::memcmp(&s.viewport, &t.viewport, sizeof(s.viewport)) != 0) return false;
    for (std::size_t i = 0; i < a.vertex_count; ++i)
        if (a.vertices[i].s != b.vertices[i].s || a.vertices[i].t != b.vertices[i].t) return false;
    return true;
}

class AnimationBlend {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t search_window = 8;
    static constexpr float teleport_distance = 100.0F; // Same bound as body interpolation.
    static constexpr std::int64_t stale_ns = 100'000'000;

    void reset() {
        previous_.reset(); current_.reset(); batches_.clear(); index_.clear(); generation_ = 0; active_ = false;
    }

    // Call once per presentation with the scene being drawn. Heavy work happens
    // only when a new generation arrives.
    void observe(std::shared_ptr<const N64DecodedFrame> frame, std::uint64_t generation,
            Clock::time_point decoded_at, bool eligible) {
        if (!frame || !eligible) { reset(); return; }
        if (current_ && generation == generation_) return;
        // A skipped generation still blends from the last shown scene when the
        // time gap is small; dropping out would jump back to an older pose.
        const bool consecutive = current_ && generation > generation_ &&
                decoded_at > current_time_ &&
                std::chrono::duration_cast<std::chrono::nanoseconds>(decoded_at - current_time_).count() <= stale_ns;
        previous_ = consecutive ? current_ : nullptr;
        previous_time_ = current_time_;
        current_ = std::move(frame);
        current_time_ = decoded_at;
        generation_ = generation;
        const auto start = Clock::now();
        rebuild();
        rebuild_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        ++rebuilds_;
    }

    // Fraction of the way from previous to current scene at now - delay.
    float alpha(Clock::time_point now, std::int64_t delay_ns) const {
        if (!previous_ || delay_ns <= 0) return 1.0F;
        const auto span = std::chrono::duration<double, std::nano>(current_time_ - previous_time_).count();
        if (span <= 0) return 1.0F;
        const auto target = now - std::chrono::nanoseconds(delay_ns);
        const auto into = std::chrono::duration<double, std::nano>(target - previous_time_).count();
        return static_cast<float>(std::clamp(into / span, 0.0, 1.0));
    }

    // Per presentation: one lerp and one projection per matched vertex.
    void prepare(Clock::time_point now, std::int64_t delay_ns) {
        const auto start = Clock::now();
        const float a = alpha(now, delay_ns);
        active_ = previous_ && a < 1.0F && !batches_.empty();
        if (active_) {
            for (auto& batch : batches_) {
                for (std::size_t i = 0; i < batch.from.size(); ++i) {
                    const double p[4]{
                        batch.from[i][0] + double(a) * (batch.to[i][0] - batch.from[i][0]),
                        batch.from[i][1] + double(a) * (batch.to[i][1] - batch.from[i][1]),
                        batch.from[i][2] + double(a) * (batch.to[i][2] - batch.from[i][2]), 1.0};
                    double clip[4]{};
                    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) clip[r] += double(batch.projection[c * 4 + r]) * p[c];
                    auto& v = batch.output[i];
                    v.x = float(clip[0]); v.y = float(clip[1]); v.z = float(clip[2]); v.w = float(clip[3]);
                }
            }
        }
        prepare_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        ++prepares_;
        report();
    }

    // Blended replacement for a current-scene batch, or nullptr to draw it as is.
    const N64RawFast3DVertex* find(const N64RawFast3DVertex* key, std::size_t count) const {
        if (!active_ || !key) return nullptr;
        const auto it = std::lower_bound(index_.begin(), index_.end(), key,
                [](const std::pair<const N64RawFast3DVertex*, std::size_t>& e, const N64RawFast3DVertex* k) { return e.first < k; });
        if (it == index_.end() || it->first != key) return nullptr;
        const auto& batch = batches_[it->second];
        return batch.output.size() == count ? batch.output.data() : nullptr;
    }

    std::size_t matched_batches() const { return batches_.size(); }
    std::size_t total_batches() const { return total_batches_; }
    std::size_t blended_vertices() const { return blended_vertices_; }

private:
    struct Batch {
        const N64RawFast3DVertex* key;
        std::array<float, 16> projection;
        std::vector<std::array<float, 3>> from, to;
        std::vector<N64RawFast3DVertex> output;
    };
    std::shared_ptr<const N64DecodedFrame> previous_, current_;
    Clock::time_point previous_time_{}, current_time_{};
    std::uint64_t generation_ = 0;
    std::vector<Batch> batches_;
    std::vector<std::pair<const N64RawFast3DVertex*, std::size_t>> index_;
    bool active_ = false;
    std::size_t total_batches_ = 0, blended_vertices_ = 0;
    std::int64_t rebuild_ns_ = 0, prepare_ns_ = 0;
    std::uint64_t rebuilds_ = 0, prepares_ = 0;
    Clock::time_point reported_ = Clock::now();

    void rebuild() {
        batches_.clear(); index_.clear(); blended_vertices_ = 0;
        const auto current = current_->triangle_batch_views();
        total_batches_ = current.size();
        if (!previous_) return;
        const auto previous = previous_->triangle_batch_views();
        // Identical-looking meshes (arms, weapon parts) can change draw order
        // between scenes, so pair each batch with the closest unused look-alike.
        std::size_t next = 0;
        std::vector<bool> used(previous.size(), false);
        for (const auto& cur : current) {
            std::size_t found = previous.size();
            double best = 0;
            Batch best_batch;
            for (std::size_t j = next; j < previous.size() && j < next + search_window; ++j) {
                if (used[j] || !animation_batches_match(previous[j], cur)) continue;
                Batch candidate;
                double distance = 0;
                if (!build_pair(previous[j], cur, candidate, distance)) continue;
                if (found == previous.size() || distance < best) {
                    found = j; best = distance; best_batch = std::move(candidate);
                }
            }
            if (found == previous.size()) continue;
            used[found] = true;
            while (next < previous.size() && used[next]) ++next;
            if (!best_batch.output.empty()) {
                blended_vertices_ += best_batch.output.size();
                batches_.push_back(std::move(best_batch));
            }
        }
        index_.reserve(batches_.size());
        for (std::size_t i = 0; i < batches_.size(); ++i) index_.push_back({batches_[i].key, i});
        std::sort(index_.begin(), index_.end());
    }

    // False when the pair cannot be blended. A valid static pair returns true
    // with an empty output (it is consumed, but drawn as is).
    static bool build_pair(const N64DecodedFrame::TriangleBatchView& prev,
            const N64DecodedFrame::TriangleBatchView& cur, Batch& batch, double& distance) {
        std::array<double, 16> inverse_prev{}, inverse_cur{};
        if (!animation_invert4(prev.state->source_projection, inverse_prev) ||
                !animation_invert4(cur.state->source_projection, inverse_cur)) return false;
        batch = Batch{cur.vertices, cur.state->source_projection, {}, {}, {}};
        const std::size_t n = cur.vertex_count;
        // World frame: previous pose through the previous camera. Camera frame:
        // previous pose through the current camera, i.e. as if it moved with it.
        std::vector<std::array<float, 3>> world(n), relative(n);
        batch.to.resize(n);
        double world_distance = 0, relative_distance = 0;
        const auto d2 = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
            double sum = 0;
            for (int k = 0; k < 3; ++k) { const double d = double(a[k]) - b[k]; sum += d * d; }
            return sum;
        };
        double world_max = 0, relative_max = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!animation_unproject(inverse_prev, prev.vertices[i], world[i]) ||
                    !animation_unproject(inverse_cur, prev.vertices[i], relative[i]) ||
                    !animation_unproject(inverse_cur, cur.vertices[i], batch.to[i])) return false;
            const double w = d2(batch.to[i], world[i]), r = d2(batch.to[i], relative[i]);
            world_distance += w; relative_distance += r;
            world_max = (std::max)(world_max, w); relative_max = (std::max)(relative_max, r);
        }
        // World frame for scenery and other actors. Camera-relative only when the
        // mesh clearly travels with the camera (first-person arms and weapons):
        // its camera-relative motion must be under a quarter of its world motion.
        const bool use_relative = relative_distance < 0.25 * world_distance;
        const double moved_max = use_relative ? relative_max : world_max;
        distance = use_relative ? relative_distance : world_distance;
        if (moved_max > double(teleport_distance) * teleport_distance) return false;
        if (moved_max > 1e-6) { // Ignore unprojection rounding (< 0.001 units).
            batch.from = use_relative ? std::move(relative) : std::move(world);
            batch.output.assign(cur.vertices, cur.vertices + n);
        } else {
            batch.to.clear();
        }
        return true;
    }

    void report() {
        if (!animation_blend_stats_enabled()) return;
        const auto now = Clock::now();
        if (now - reported_ < std::chrono::seconds(5)) return;
        std::fprintf(stderr,
                "ANIMATION_BLEND_STATS matched=%zu/%zu vertices=%zu rebuild_us=%.1f prepare_us=%.1f rebuilds=%llu prepares=%llu\n",
                batches_.size(), total_batches_, blended_vertices_,
                rebuilds_ ? rebuild_ns_ / 1000.0 / double(rebuilds_) : 0.0,
                prepares_ ? prepare_ns_ / 1000.0 / double(prepares_) : 0.0,
                static_cast<unsigned long long>(rebuilds_), static_cast<unsigned long long>(prepares_));
        reported_ = now; rebuild_ns_ = prepare_ns_ = 0; rebuilds_ = prepares_ = 0;
    }
};

} // namespace xr64::rage_wars::recomp
