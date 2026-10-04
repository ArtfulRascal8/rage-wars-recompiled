#pragma once
#include "rage_wars_openxr.hpp"
#include <array>
#include <cmath>

namespace xr64::rage_wars::recomp {
using XrShotQuat = std::array<float, 4>;

inline bool xr_normalize_shot_quat(XrShotQuat& q) {
    float length2 = 0.0F;
    for (float value : q) {
        if (!std::isfinite(value)) return false;
        length2 += value * value;
    }
    if (!std::isfinite(length2) || length2 < 0.5F || length2 > 1.5F) return false;
    const float inv = 1.0F / std::sqrt(length2);
    for (float& value : q) value *= inv;
    return true;
}

// The native camera is the origin of recentered tracking space. Head motion
// is applied later by the XR renderer and must not be subtracted here.
inline XrShotQuat xr_shot_multiply(const XrShotQuat& a,const XrShotQuat& b) {
    const auto [ax,ay,az,aw]=a;
    const auto [bx,by,bz,bw]=b;
    return {aw*bx+ax*bw+ay*bz-az*by,
            aw*by-ax*bz+ay*bw+az*bx,
            aw*bz+ax*by-ay*bx+az*bw,
            aw*bw-ax*bx-ay*by-az*bz};
}
inline bool xr_compose_shot_aim(const XrShotQuat& game_aim,
        const XrMotionInput& motion, XrShotQuat& result) {
    if (!motion.head_pose_valid || !motion.right.aim_pose_valid) return false;
    XrShotQuat game=game_aim;
    XrShotQuat hand{motion.right.aim_orientation[0],motion.right.aim_orientation[1],
                    motion.right.aim_orientation[2],motion.right.aim_orientation[3]};
    if (!xr_normalize_shot_quat(game) ||
            !xr_normalize_shot_quat(hand)) return false;
    result=xr_shot_multiply(game,hand);
    return xr_normalize_shot_quat(result);
}
// Invert exactly the normalized X/Y/-W coordinates consumed by
// xr_project_vertex. The guest display projection includes an extra rotation
// and X-axis scaling, so view+0x160 is NOT the renderer's tracking basis.
inline bool xr_tracking_to_world(const std::array<float,16>& projection,
        std::array<float,16>& camera) {
    for(float v:projection) if(!std::isfinite(v)) return false;
    constexpr int columns[3]={0,1,3};
    std::array<float,3> translation{};
    camera={};camera[15]=1;
    for(int axis=0;axis<3;++axis) {
        const int col=columns[axis];
        float norm=0;
        for(int i=0;i<3;++i) norm+=projection[i*4+col]*projection[i*4+col];
        norm=std::sqrt(norm);
        if(norm<0.00001F) return false;
        const float scale=(axis==2?-1.0F:1.0F)/norm;
        for(int i=0;i<3;++i) camera[axis*4+i]=projection[i*4+col]*scale;
        translation[axis]=projection[12+col]*scale;
    }
    // Rigid basis only; reject a skewed or transient projection.
    for(int a=0;a<3;++a) for(int b=a+1;b<3;++b) {
        float dot=0;
        for(int i=0;i<3;++i) dot+=camera[a*4+i]*camera[b*4+i];
        if(std::abs(dot)>0.0001F) return false;
    }
    for(int i=0;i<3;++i) for(int axis=0;axis<3;++axis)
        camera[12+i]-=translation[axis]*camera[axis*4+i];
    return true;
}

// Positions use the renderer's guest units/metre. A distant target preserves
// native projectile spread. camera must be the inverse rendered basis above.
inline bool xr_world_shot_ray(const std::array<float,16>& camera,
        const XrMotionInput& motion, std::array<float,3>& origin,
        std::array<float,3>& target, float units=100.0F) {
    if (!motion.head_pose_valid || !motion.right.aim_pose_valid ||
            !std::isfinite(units) || units<=0) return false;
    for (float v:camera) if (!std::isfinite(v)) return false;
    for (int i=0;i<3;++i) {
        float length=0;
        for (int j=0;j<3;++j) length+=camera[i*4+j]*camera[i*4+j];
        if (std::abs(length-1.0F)>0.02F) return false;
    }
    XrShotQuat q;
    for(int i=0;i<4;++i) q[i]=motion.right.aim_orientation[i];
    if (!xr_normalize_shot_quat(q)) return false;
    const auto [x,y,z,w]=q;
    const std::array<float,3> direction{-2*(x*z+w*y),-2*(y*z-w*x),-1+2*(x*x+y*y)};
    for(int i=0;i<3;++i) {
        origin[i]=camera[12+i];
        float forward=0;
        for(int j=0;j<3;++j) {
            const float p=motion.right.aim_position[j];
            if (!std::isfinite(p) || std::abs(p)>10) return false;
            origin[i]+=units*p*camera[j*4+i];
            forward+=direction[j]*camera[j*4+i];
        }
        target[i]=origin[i]+10000.0F*forward;
    }
    return true;
}
// Reorient the native first-person model in tracking space. Its original
// scale/mesh-axis conversion is retained; the model pivot is placed at aim pose.
// This is pivot placement, not a calibrated grip/bone attachment.
inline bool xr_tracked_weapon_matrix(const std::array<float,16>& native_camera,
        const std::array<float,16>& rendered_camera,const XrMotionInput& motion,
        const std::array<float,16>& original,std::array<float,16>& result,
        float units=100.0F) {
    std::array<float,3> origin{},target{};
    if(!xr_world_shot_ray(rendered_camera,motion,origin,target,units)) return false;
    for(float v:original) if(!std::isfinite(v)) return false;
    for(float v:native_camera) if(!std::isfinite(v)) return false;
    XrShotQuat q;
    for(int i=0;i<4;++i) q[i]=motion.right.aim_orientation[i];
    if(!xr_normalize_shot_quat(q)) return false;
    result={};result[15]=1;
    for(int row=0;row<3;++row) {
        std::array<float,3> v{};
        for(int j=0;j<3;++j) for(int i=0;i<3;++i)
            v[j]+=original[row*4+i]*native_camera[j*4+i];
        // Guest's final display convention mirrors early-camera X.
        v[0]=-v[0];
        const float tx=2*(q[1]*v[2]-q[2]*v[1]);
        const float ty=2*(q[2]*v[0]-q[0]*v[2]);
        const float tz=2*(q[0]*v[1]-q[1]*v[0]);
        v[0]+=q[3]*tx+q[1]*tz-q[2]*ty;
        v[1]+=q[3]*ty+q[2]*tx-q[0]*tz;
        v[2]+=q[3]*tz+q[0]*ty-q[1]*tx;
        for(int i=0;i<3;++i) for(int j=0;j<3;++j)
            result[row*4+i]+=v[j]*rendered_camera[j*4+i];
    }
    for(int i=0;i<3;++i) result[12+i]=origin[i];
    return true;
}
} // namespace xr64::rage_wars::recomp
