#include "rage_wars_presentation_clock.hpp"
#include <chrono>
#include <cmath>

using namespace xr64::rage_wars::recomp;

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    HostPresentationClock clock;
    const Clock::time_point start{1s};
    auto first = clock.sample(HostPresentationMode::Desktop, 4, start - 10ms, start);
    if (first.sequence != 1 || first.raw_elapsed != Clock::duration{} ||
            first.repeated_generation || !first.decoded_age_known ||
            first.decoded_age != 10ms) return 1;
    auto repeat = clock.sample(HostPresentationMode::Desktop, 4, start, start + 16ms);
    if (!repeat.repeated_generation || repeat.raw_elapsed != 16ms ||
            std::abs(repeat.visual_delta_seconds - 0.016) > 0.000001) return 2;
    auto stall = clock.sample(HostPresentationMode::Desktop, 5, start,
            start + 1016ms);
    if (stall.raw_elapsed != 1000ms || stall.visual_delta_seconds != 0.0 ||
            stall.repeated_generation) return 3;
    auto xr = clock.sample(HostPresentationMode::OpenXR, 5,
            Clock::time_point{}, start + 1032ms);
    if (xr.raw_elapsed != 16ms || xr.visual_delta_seconds != 0.0 ||
            xr.repeated_generation || xr.decoded_age_known) return 4;
    clock.reset();
    auto resumed = clock.sample(HostPresentationMode::OpenXR, 5, start,
            start + 1048ms);
    if (resumed.sequence != 5 || resumed.raw_elapsed != Clock::duration{} ||
            resumed.repeated_generation) return 5;
    return 0;
}
