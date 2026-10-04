#pragma once
#include <cmath>

namespace xr64::rage_wars::recomp {
// Use vsync while it demonstrably paces the application. Drivers may accept
// the request yet override it; after eight fast frames, fall back to the active
// monitor's refresh period. XR never uses this desktop policy.
struct DesktopPacingPolicy {
    int refresh_hz=60;
    unsigned fast_frames=0;
    bool software=false;
    long long period_ns() const {return 1'000'000'000LL/refresh_hz;}
    void refresh(int hz) {
        if(hz<20 || hz>1000)hz=60;
        if(hz==refresh_hz)return;
        refresh_hz=hz;fast_frames=0;software=false;
    }
    void frame(long long elapsed,bool vsync_accepted) {
        if(software)return;
        if(!vsync_accepted){software=true;return;}
        if(elapsed<period_ns()/2)++fast_frames;else fast_frames=0;
        if(fast_frames>=8)software=true;
    }
};
struct DesktopProjectionAdjustment {
    bool wide = false;
    float x_scale = 1.0F;
    float y_scale = 1.0F;
};

// Menu models share guest pixel coordinates with 2D artwork. Camera
// preferences apply only to gameplay, regardless of the active output mode.
inline DesktopProjectionAdjustment desktop_projection_adjustment(
        bool menu_panel, bool xr_active, bool perspective, bool scene_viewport,
        bool widescreen, int width, int height, float fov, float guest_y_scale) {
    DesktopProjectionAdjustment result;
    if (menu_panel || !perspective || !scene_viewport) return result;
    result.wide = widescreen && height > 0 && width * 3.0 > height * 4.0;
    if (!xr_active && fov > 0.0F && guest_y_scale > 0.00001F) {
        result.x_scale = result.y_scale =
                1.0F / (std::tan(fov * 0.00872664626F) * guest_y_scale);
    }
    if (result.wide) result.x_scale *= (4.0F / 3.0F) * height / width;
    return result;
}
}
