#pragma once
#include "rage_wars_controls.hpp"
#include <chrono>
#include <cstdlib>
namespace xr64::rage_wars::recomp {
// Explicit, finite diagnostic at the same relative-count ingress used by SDL.
// Normal launches never synthesize input. This is pipeline evidence, not a
// physical mouse or headset feel test.
inline bool control_sample_enabled() {
    static const bool enabled=[]{const char* v=std::getenv("XR64_LOCAL_CONTROL_SAMPLE");return v && v[0]=='1' && v[1]==0;}();
    return enabled;
}
struct RelativeControlSample {
    std::chrono::steady_clock::time_point started{};
    unsigned step=0;
    controls::AxisPair next(bool ready,const controls::Settings& settings) {
        if(!ready || step>=8)return {};
        const auto now=std::chrono::steady_clock::now();
        if(started==std::chrono::steady_clock::time_point{})started=now;
        const auto seconds=std::chrono::duration<double>(now-started).count();
        const double times[]={2,3,4,5,6,7,8,9};
        if(seconds<times[step])return {};
        const float center=std::round(90.0F/(0.08F*settings.mouse_sensitivity_y))*(settings.mouse_invert_y?-1.0F:1.0F);
        const controls::AxisPair counts[]={{120,0},{-120,0},{0,3000},{0,-6000},{0,3000},{0,center},{12,6},{-12,-6}};
        return counts[step++];
    }
};
}
