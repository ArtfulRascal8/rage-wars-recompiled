#pragma once
#include "rage_wars_xr_hand_ray.hpp"
#include "rage_wars_startup_transition.hpp"
namespace xr64::rage_wars::recomp {
// A 2m x 1.5m screen two metres ahead of the recentered tracking origin.
inline auto xr_menu_corner(float x,float y,const XrEyeFrame& eye,float distance=2.0F,float width=2.0F) {
 return xr_project_tracked_point({x*width*0.5F,y*0.75F*width*0.5F,-distance},eye);
}
inline bool xr_startup_white_fill(bool startup,float left,float top,float right,float bottom,
        int width,int height,const ::xr64::N64RawFast3DDrawState& state) {
 return startup_white_fill(startup,left,top,right,bottom,width,height,state);
}
}
