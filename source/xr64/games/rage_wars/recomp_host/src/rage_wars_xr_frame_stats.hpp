#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace xr64::rage_wars::recomp {

// Opt-in VR frame summary (XR64_VR_FRAME_STATS=1), kept in a header so consumer
// source preparation does not strip it. Prints medians and maxima every 5 s.
inline bool xr_frame_stats_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("XR64_VR_FRAME_STATS");
        return v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}

// Shot-hook evidence (entries, early-out reasons, applied shots), printed here
// because consumer preparation strips diagnostics from .cpp files.
inline void xr_report_shot_hook(const unsigned long long (&outcomes)[10],
        unsigned long long shots, unsigned long long barrel, double lead_units) {
    if (!xr_frame_stats_enabled()) return;
    static unsigned long long reported_entries = ~0ULL, reported_shots = ~0ULL;
    if (outcomes[0] == reported_entries && shots == reported_shots) return;
    reported_entries = outcomes[0]; reported_shots = shots;
    std::fprintf(stderr, "RW_XR_SHOT_HOOK entries=%llu disabled=%llu not_local=%llu state=%llu weapon=%llu "
        "no_pose=%llu stale_pose=%llu view=%llu camera=%llu ray=%llu applied=%llu barrel=%llu last_lead_units=%.2f\n",
        outcomes[0], outcomes[1], outcomes[2], outcomes[3], outcomes[4], outcomes[5], outcomes[6],
        outcomes[7], outcomes[8], outcomes[9], shots, barrel, lead_units);
}

// How old the presented scene is compared with the camera shown over it. The
// scene (world objects, muzzle flash, impact marks) sits at the game's sample
// time; the displayed camera is the body history at now - delay. camera_ahead =
// scene age - delay: when positive, the camera is that many ms ahead of the
// scene's dynamic content, so flash and impacts trail by speed * camera_ahead.
// Printed every 5 s with XR64_VR_FRAME_STATS=1 (desktop and VR paths).
class SceneAgeStats {
public:
    void record(double age_ms, double delay_ms, std::uint64_t generation) {
        if (!xr_frame_stats_enabled()) return;
        const auto now = std::chrono::steady_clock::now();
        if (start_ == std::chrono::steady_clock::time_point{}) start_ = now;
        if (generation != last_generation_) { ++scenes_; last_generation_ = generation; }
        age_.push_back(age_ms);
        ahead_.push_back(age_ms - delay_ms);
        delay_ = delay_ms;
        if (now - start_ < std::chrono::seconds(5)) return;
        report(std::chrono::duration<double>(now - start_).count());
        age_.clear(); ahead_.clear(); scenes_ = 0;
        start_ = now;
    }
private:
    std::vector<double> age_, ahead_;
    std::uint64_t last_generation_ = ~0ULL, scenes_ = 0;
    double delay_ = 0;
    std::chrono::steady_clock::time_point start_{};
    static void quantiles(std::vector<double> v, double (&out)[3]) {
        std::sort(v.begin(), v.end());
        out[0] = v[v.size() / 2];
        out[1] = v[std::min(v.size() - 1, std::size_t(double(v.size()) * 0.9))];
        out[2] = v.back();
    }
    void report(double seconds) const {
        if (age_.empty()) return;
        double age[3], ahead[3];
        quantiles(age_, age);
        quantiles(ahead_, ahead);
        std::fprintf(stderr,
            "RW_SCENE_AGE presentations=%zu scenes_per_s=%.1f delay_ms=%.0f scene_age_ms=%.1f/%.1f/%.1f "
            "camera_ahead_ms=%.1f/%.1f/%.1f (median/p90/max)\n",
            age_.size(), double(scenes_) / seconds, delay_, age[0], age[1], age[2], ahead[0], ahead[1], ahead[2]);
    }
};

struct XrFrameSample {
    double period_ms, wait_ms, app_gap_ms, left_ms, right_ms, upload_ms, end_ms, gpu_ms, decode_ms;
    std::uint64_t misses, draws, vertices;
    bool repeated;
    std::uint64_t clip_vertices, alpha_draws, immediate_draws, rects;
    double opaque_mpx, alpha_mpx, prepare_ms, submit_ms;
};

class XrFrameStats {
public:
    void record(const XrFrameSample& s) {
        if (!xr_frame_stats_enabled()) return;
        samples_.push_back(s);
        const auto now = std::chrono::steady_clock::now();
        if (start_ == std::chrono::steady_clock::time_point{}) start_ = now;
        if (now - start_ < std::chrono::seconds(5)) return;
        report(std::chrono::duration<double>(now - start_).count());
        samples_.clear();
        start_ = now;
    }
private:
    std::vector<XrFrameSample> samples_;
    std::chrono::steady_clock::time_point start_{};
    template<class F> std::pair<double, double> med_max(F f) const {
        std::vector<double> v;
        v.reserve(samples_.size());
        for (const auto& s : samples_) v.push_back(f(s));
        if (v.empty()) return {0, 0};
        std::sort(v.begin(), v.end());
        return {v[v.size() / 2], v.back()};
    }
    void report(double seconds) const {
        std::uint64_t misses = 0, repeats = 0;
        for (const auto& s : samples_) { misses += s.misses; repeats += s.repeated ? 1 : 0; }
        const auto period = med_max([](const XrFrameSample& s) { return s.period_ms; });
        const auto wait = med_max([](const XrFrameSample& s) { return s.wait_ms; });
        const auto gap = med_max([](const XrFrameSample& s) { return s.app_gap_ms; });
        const auto cpu = med_max([](const XrFrameSample& s) { return s.left_ms + s.right_ms; });
        const auto upload = med_max([](const XrFrameSample& s) { return s.upload_ms; });
        const auto end = med_max([](const XrFrameSample& s) { return s.end_ms; });
        const auto gpu = med_max([](const XrFrameSample& s) { return s.gpu_ms; });
        const auto decode = med_max([](const XrFrameSample& s) { return s.decode_ms; });
        const auto draws = med_max([](const XrFrameSample& s) { return double(s.draws); });
        const auto verts = med_max([](const XrFrameSample& s) { return double(s.vertices); });
        const auto clipped = med_max([](const XrFrameSample& s) { return double(s.clip_vertices); });
        const auto alpha_draws = med_max([](const XrFrameSample& s) { return double(s.alpha_draws); });
        const auto immediate = med_max([](const XrFrameSample& s) { return double(s.immediate_draws); });
        const auto rects = med_max([](const XrFrameSample& s) { return double(s.rects); });
        const auto opaque = med_max([](const XrFrameSample& s) { return s.opaque_mpx; });
        const auto alpha = med_max([](const XrFrameSample& s) { return s.alpha_mpx; });
        const auto prepare = med_max([](const XrFrameSample& s) { return s.prepare_ms; });
        const auto submit = med_max([](const XrFrameSample& s) { return s.submit_ms; });
        std::fprintf(stderr,
            "VR_FRAME_STATS fps=%.1f period_ms=%.2f misses=%llu repeated=%llu wait_ms=%.2f/%.2f app_gap_ms=%.2f/%.2f "
            "eyes_cpu_ms=%.2f/%.2f upload_ms=%.2f/%.2f end_ms=%.2f/%.2f gpu_ms=%.2f/%.2f decode_ms=%.2f/%.2f "
            "draws=%.0f/%.0f vertices=%.0f/%.0f clipped=%.0f/%.0f alpha_draws=%.0f/%.0f immediate=%.0f/%.0f rects=%.0f/%.0f "
            "opaque_mpx=%.2f/%.2f alpha_mpx=%.2f/%.2f prepare_ms=%.2f/%.2f submit_ms=%.2f/%.2f\n",
            samples_.size() / seconds, period.first, static_cast<unsigned long long>(misses),
            static_cast<unsigned long long>(repeats), wait.first, wait.second, gap.first, gap.second,
            cpu.first, cpu.second, upload.first, upload.second, end.first, end.second, gpu.first, gpu.second,
            decode.first, decode.second, draws.first, draws.second, verts.first, verts.second, clipped.first, clipped.second,
            alpha_draws.first, alpha_draws.second, immediate.first, immediate.second, rects.first, rects.second,
            opaque.first, opaque.second, alpha.first, alpha.second, prepare.first, prepare.second, submit.first, submit.second);
    }
};

} // namespace xr64::rage_wars::recomp
