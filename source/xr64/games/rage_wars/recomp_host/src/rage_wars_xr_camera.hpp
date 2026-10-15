#pragma once
#include "rage_wars_openxr.hpp"
#include "xr64_fast3d/n64_raw_fast3d_renderer.hpp"
#include <cmath>
namespace xr64::rage_wars::recomp {
inline ::xr64::N64RawFast3DVertex xr_project_vertex(::xr64::N64RawFast3DVertex v,
        const ::xr64::N64RawFast3DDrawState& state,const XrEyeFrame& eye,float units) {
    const float l=std::tan(eye.fov[0]),r=std::tan(eye.fov[1]),u=std::tan(eye.fov[2]),d=std::tan(eye.fov[3]);
    const float qx=-eye.orientation[0],qy=-eye.orientation[1],qz=-eye.orientation[2],qw=eye.orientation[3];
    float x=v.x/state.perspective_x_norm-eye.position[0]*units;
    float y=v.y/state.perspective_y_norm-eye.position[1]*units;
    float z=-v.w/state.perspective_w_norm-eye.position[2]*units;
    const float tx=2*(qy*z-qz*y),ty=2*(qz*x-qx*z),tz=2*(qx*y-qy*x);
    x+=qw*tx+qy*tz-qz*ty;y+=qw*ty+qz*tx-qx*tz;z+=qw*tz+qx*ty-qy*tx;
    const float nearZ=units*0.02F,farZ=units*1000.0F;
    v.x=2*x/(r-l)+(r+l)*z/(r-l);
    v.y=2*y/(u-d)+(u+d)*z/(u-d);
    v.z=-(farZ+nearZ)*z/(farZ-nearZ)-2*farZ*nearZ/(farZ-nearZ);v.w=-z;
    return v;
}
// Same arithmetic as xr_project_vertex with the per-eye constants hoisted out
// of the vertex loop (identical results; one set of tan() calls per draw).
struct XrEyeProjector {
    float l,r,u,d,qx,qy,qz,qw,px,py,pz,nearZ,farZ,xn,yn,wn;
    XrEyeProjector(const ::xr64::N64RawFast3DDrawState& state,const XrEyeFrame& eye,float units)
        : l(std::tan(eye.fov[0])),r(std::tan(eye.fov[1])),u(std::tan(eye.fov[2])),d(std::tan(eye.fov[3])),
          qx(-eye.orientation[0]),qy(-eye.orientation[1]),qz(-eye.orientation[2]),qw(eye.orientation[3]),
          px(eye.position[0]*units),py(eye.position[1]*units),pz(eye.position[2]*units),
          nearZ(units*0.02F),farZ(units*1000.0F),
          xn(state.perspective_x_norm),yn(state.perspective_y_norm),wn(state.perspective_w_norm) {}
    void operator()(::xr64::N64RawFast3DVertex& v) const {
        float x=v.x/xn-px;
        float y=v.y/yn-py;
        float z=-v.w/wn-pz;
        const float tx=2*(qy*z-qz*y),ty=2*(qz*x-qx*z),tz=2*(qx*y-qy*x);
        x+=qw*tx+qy*tz-qz*ty;y+=qw*ty+qz*tx-qx*tz;z+=qw*tz+qx*ty-qy*tx;
        v.x=2*x/(r-l)+(r+l)*z/(r-l);
        v.y=2*y/(u-d)+(u+d)*z/(u-d);
        v.z=-(farZ+nearZ)*z/(farZ-nearZ)-2*farZ*nearZ/(farZ-nearZ);v.w=-z;
    }
};
}
