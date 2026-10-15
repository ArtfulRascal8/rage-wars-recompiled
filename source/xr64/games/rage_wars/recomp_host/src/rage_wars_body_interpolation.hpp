#pragma once

#include "rage_wars_local_presentation.hpp"
#include <array>
#include <charconv>
#include <cstring>

namespace xr64::rage_wars::recomp {

// Visual body delay in integer milliseconds (default 80). No inferred guest tick rate.
// Completion times share the producer's steady_clock domain; they are not
// simulation timestamps. This introduces visual body latency only.
inline std::int64_t parse_local_body_delay_ns(const char* value) {
    if (!value) return 0;
    int milliseconds = 0;
    const auto end = value + std::strlen(value);
    const auto parsed = std::from_chars(value, end, milliseconds);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
            milliseconds < 1 || milliseconds > 100) return 0;
    return std::int64_t(milliseconds) * 1'000'000;
}

inline std::int64_t local_body_delay_ns() {
    // 80 ms unless XR64_LOCAL_BODY_DELAY_MS says otherwise ("0" turns smoothing off).
    static const auto delay = [] {
        const char* value = std::getenv("XR64_LOCAL_BODY_DELAY_MS");
        return value ? parse_local_body_delay_ns(value) : std::int64_t(80) * 1'000'000;
    }();
    return delay;
}

inline bool local_frame_sample_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("XR64_LOCAL_FRAME_SAMPLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Single presentation-owner history. Observe completed publications, never RAM
// or guest work. A missed publication breaks the segment instead of guessing
// its collision path. The producer/mailbox and decoded payload stay unchanged.
class LocalBodyInterpolation {
    struct Sample {
        std::int64_t time = 0;
        std::uint64_t update = 0;
        std::array<float, 3> origin{};
        float yaw = 0;
    };
    bool interpolate_yaw_ = false;
    std::array<Sample, 32> samples_{};
    std::size_t count_ = 0;
    std::uint32_t actor_ = 0;
    std::uint64_t continuity_ = 0, input_epoch_ = 0;
    std::int64_t previous_now_ = 0;
    bool trimmed_ = false;
    static constexpr std::int64_t stale_ns = 250'000'000;

    void start(const LocalPresentationState& current) {
        count_ = 1;
        trimmed_ = false;
        actor_ = current.player.actor;
        continuity_ = current.player.continuity;
        input_epoch_ = current.mouse.epoch;
        samples_[0] = {current.player.sampled_ns, current.player.update,
                current.player.origin, current.player.yaw};
    }
public:
    // VR: also interpolate body yaw (stick turning updates it only per game
    // tick). Desktop keeps its fresh per-frame mouse yaw.
    void set_interpolate_yaw(bool enabled) { interpolate_yaw_ = enabled; }
    void reset() {
        count_ = 0;
        previous_now_ = 0;
        trimmed_ = false;
    }

    LocalCameraCorrection camera(const LocalPlayerPresentation& scene,
            const LocalPresentationState& current, std::int64_t now,
            std::int64_t delay_ns) {
        auto correction = local_camera_correction(scene, current, now);
        if (delay_ns <= 0 || delay_ns > 100'000'000 || !correction.valid ||
                current.player.sampled_ns <= 0 || current.player.update == 0) {
            reset();
            return correction;
        }
        for (const float coordinate : current.player.origin) {
            if (!std::isfinite(coordinate)) {
                reset();
                correction.valid = false;
                return correction;
            }
        }
        if (previous_now_ && (now < previous_now_ ||
                now - previous_now_ > stale_ns)) reset();
        previous_now_ = now;

        const Sample sample{current.player.sampled_ns, current.player.update,
                current.player.origin, current.player.yaw};
        if (!count_ || actor_ != current.player.actor ||
                continuity_ != current.player.continuity ||
                input_epoch_ != current.mouse.epoch) {
            start(current);
        } else {
            const auto& last = samples_[count_ - 1];
            if (sample.update == last.update) {
                // A completed publication is immutable at the consumer.
                if (sample.time != last.time || sample.origin != last.origin)
                    start(current);
            } else {
                double distance2 = 0;
                for (int i = 0; i < 3; ++i) {
                    const double d = double(sample.origin[i]) - last.origin[i];
                    distance2 += d * d;
                }
                if (sample.update <= last.update || sample.update - last.update != 1 ||
                        sample.time <= last.time || sample.time - last.time > stale_ns ||
                        distance2 > 10'000) {
                    start(current);
                } else {
                    if (count_ == samples_.size()) {
                        for (std::size_t i = 1; i < count_; ++i)
                            samples_[i - 1] = samples_[i];
                        --count_;
                        trimmed_ = true;
                    }
                    samples_[count_++] = sample;
                }
            }
        }

        const auto target_time = now - delay_ns;
        auto origin = samples_[0].origin;
        float yaw = samples_[0].yaw;
        if (target_time < samples_[0].time && trimmed_) {
            // Bounded history could not cover the chosen delay. Start a new
            // segment rather than interpolating an unavailable interval.
            start(current);
            origin = current.player.origin;
            yaw = current.player.yaw;
        } else if (target_time >= samples_[count_ - 1].time) {
            origin = samples_[count_ - 1].origin; // Hold; never extrapolate.
            yaw = samples_[count_ - 1].yaw;
        } else {
            for (std::size_t i = 1; i < count_; ++i) {
                const auto& a = samples_[i - 1];
                const auto& b = samples_[i];
                if (target_time < a.time || target_time > b.time) continue;
                const double alpha = double(target_time - a.time) / double(b.time - a.time);
                for (int axis = 0; axis < 3; ++axis)
                    origin[axis] = float(double(a.origin[axis]) +
                            alpha * (double(b.origin[axis]) - a.origin[axis]));
                yaw = float(double(a.yaw) + alpha * std::remainder(double(b.yaw) - a.yaw, 6.283185307179586));
                break;
            }
        }
        // Change only the display translation. Keep fresh yaw/pitch/mouse,
        // endpoint IDs and all eligibility guards from the existing helper.
        for (int i = 0; i < 3; ++i)
            correction.target[12 + i] += origin[i] - current.player.origin[i];
        if (interpolate_yaw_) {
            // Rotate the displayed camera about world up by the smoothed-yaw lag,
            // matching how local_camera_correction applies yaw.
            const float delta = std::remainder(yaw - current.player.yaw, 6.283185307179586F);
            if (std::isfinite(delta) && std::fabs(delta) < 1.0F) {
                for (int column = 0; column < 3; ++column) {
                    std::array<float,3> axis{correction.target[column*4], correction.target[column*4+1], correction.target[column*4+2]};
                    axis = rotate_axis(axis, {0, 1, 0}, delta);
                    for (int i = 0; i < 3; ++i) correction.target[column*4+i] = axis[i];
                }
                correction.yaw += delta;
            }
        }
        return correction;
    }
};

} // namespace xr64::rage_wars::recomp
