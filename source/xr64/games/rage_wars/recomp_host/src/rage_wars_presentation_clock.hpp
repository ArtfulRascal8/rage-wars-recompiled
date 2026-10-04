#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace xr64::rage_wars::recomp {

enum class HostPresentationMode { Desktop, OpenXR };

// All host times use steady_clock. A decoded_at timestamp measures decode
// completion, never the guest simulation instant. Raw OpenXR XrTime is kept
// outside this clock until an explicit runtime/host clock mapping is available.
struct HostPresentationTick {
    using Clock = std::chrono::steady_clock;
    Clock::time_point host_now{};
    Clock::duration raw_elapsed{};
    Clock::duration decoded_age{};
    double visual_delta_seconds = 0.0;
    std::uint64_t sequence = 0;
    std::uint64_t generation = 0;
    HostPresentationMode mode = HostPresentationMode::Desktop;
    bool repeated_generation = false;
    bool decoded_age_known = false;
};

class HostPresentationClock {
    using Clock = std::chrono::steady_clock;
    Clock::time_point previous_{};
    HostPresentationMode previous_mode_ = HostPresentationMode::Desktop;
    std::uint64_t sequence_ = 0;
    std::uint64_t previous_generation_ = 0;
    bool have_previous_ = false;
    bool have_generation_ = false;
public:
    void reset() {
        previous_ = {};
        have_previous_ = false;
        have_generation_ = false;
    }

    HostPresentationTick sample(HostPresentationMode mode,
            std::uint64_t generation, Clock::time_point decoded_at,
            Clock::time_point now = Clock::now()) {
        HostPresentationTick tick;
        tick.host_now = now;
        tick.sequence = ++sequence_;
        tick.generation = generation;
        tick.mode = mode;
        const bool mode_changed = have_previous_ && previous_mode_ != mode;
        if (have_previous_ && now >= previous_) tick.raw_elapsed = now - previous_;
        tick.repeated_generation = have_generation_ && !mode_changed &&
                previous_generation_ == generation;
        const auto raw_seconds =
                std::chrono::duration<double>(tick.raw_elapsed).count();
        // A pause or mode change must not jump host-only visual animations.
        if (!mode_changed && raw_seconds <= 0.25)
            tick.visual_delta_seconds = std::min(raw_seconds, 1.0 / 15.0);
        if (decoded_at != Clock::time_point{} && now >= decoded_at) {
            tick.decoded_age = now - decoded_at;
            tick.decoded_age_known = true;
        }
        previous_ = now;
        previous_mode_ = mode;
        previous_generation_ = generation;
        have_previous_ = true;
        have_generation_ = true;
        return tick;
    }
};

} // namespace xr64::rage_wars::recomp
