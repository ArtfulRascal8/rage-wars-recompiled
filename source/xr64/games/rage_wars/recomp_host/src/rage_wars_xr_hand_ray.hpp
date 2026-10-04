#pragma once
#include "rage_wars_xr_camera.hpp"
#include <array>
#include <cmath>

namespace xr64::rage_wars::recomp {
struct XrTrackedRay {
    std::array<float,3> origin{};
    std::array<float,3> forward{};
};

// OpenXR's aim pose defines the controller pointing frame. The grip frame is
// retained separately for eventual weapon placement.
inline bool xr_tracked_aim_ray(const XrMotionHand& hand,XrTrackedRay& ray) {
    if(!hand.aim_pose_valid)return false;
    const float x=hand.aim_orientation[0],y=hand.aim_orientation[1],z=hand.aim_orientation[2],w=hand.aim_orientation[3];
    const float norm=x*x+y*y+z*z+w*w;
    if(!std::isfinite(norm)||norm<0.5F||norm>1.5F)return false;
    for(float p:hand.aim_position)if(!std::isfinite(p))return false;
    const float inv=1.0F/std::sqrt(norm);
    const float qx=x*inv,qy=y*inv,qz=z*inv,qw=w*inv;
    // Rotate OpenXR -Z through the aim quaternion.
    ray.origin={hand.aim_position[0],hand.aim_position[1],hand.aim_position[2]};
    ray.forward={-2.0F*(qx*qz+qw*qy),-2.0F*(qy*qz-qw*qx),-1.0F+2.0F*(qx*qx+qy*qy)};
    return true;
}

inline ::xr64::N64RawFast3DVertex xr_project_tracked_point(
        const std::array<float,3>& point,const XrEyeFrame& eye) {
    ::xr64::N64RawFast3DDrawState state;
    state.perspective_x_norm=state.perspective_y_norm=state.perspective_w_norm=1.0F;
    ::xr64::N64RawFast3DVertex vertex;
    vertex.x=point[0];vertex.y=point[1];vertex.w=-point[2];
    return xr_project_vertex(vertex,state,eye,1.0F);
}
}
