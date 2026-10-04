#pragma once
#include "rage_wars_xr_shot.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
namespace xr64::rage_wars::recomp {
// Model-only calibration: moving a mesh must never silently redirect bullets.
struct WeaponCalibration {
    std::array<float,3> offset{}; // metres in controller aim space
    std::array<float,3> degrees{}; // pitch, yaw, roll
    float scale=1;
};
inline bool valid_weapon_calibration(const WeaponCalibration& c) {
    for(float x:c.offset)if(!std::isfinite(x)||std::abs(x)>0.5F)return false;
    for(float x:c.degrees)if(!std::isfinite(x)||std::abs(x)>90)return false;
    return std::isfinite(c.scale)&&c.scale>=0.25F&&c.scale<=2;
}
inline std::filesystem::path weapon_profile_directory() {
    if(const auto* p=std::getenv("XR64_PORT_OPTIONS_CONFIG");p&&*p)return std::filesystem::u8path(p).parent_path();
    if(const auto* p=std::getenv("LOCALAPPDATA");p&&*p)return std::filesystem::u8path(p)/"XR64"/"RageWars"/"user-data";
    return {};
}
inline std::filesystem::path weapon_asset_directory() {
    if(const auto* p=std::getenv("XR64_WEAPON_ASSETS");p&&*p)return std::filesystem::u8path(p);
    if(const auto* p=std::getenv("XR64_RW_PREVIEW_WEAPONS");p&&*p)return std::filesystem::u8path(p);
    const auto profile=weapon_profile_directory();
    if(profile.empty())return {};
    // Preserve both existing private asset layouts; an explicit folder wins.
    std::error_code ec;
    const auto models=profile/"weapon-previews";
    if(std::filesystem::is_directory(models,ec))return models;
    const auto runtime=profile/"runtime"/"preview-weapons";
    if(std::filesystem::is_directory(runtime,ec))return runtime;
    return models;
}
inline std::filesystem::path weapon_calibration_path(unsigned weapon) {
    if(weapon<1||weapon>14)return {};
    const auto profile=weapon_profile_directory();
    if(profile.empty())return {};
    return profile/"weapon-calibration"/(std::to_string(weapon)+".txt");
}
inline WeaponCalibration load_weapon_calibration(unsigned weapon) {
    WeaponCalibration c;
    std::ifstream in(weapon_calibration_path(weapon));
    unsigned version=0;
    if(!(in>>version)||version!=1)return {};
    for(auto& v:c.offset)if(!(in>>v))return {};
    for(auto& v:c.degrees)if(!(in>>v))return {};
    if(!(in>>c.scale)||!valid_weapon_calibration(c))return {};
    in>>std::ws;if(!in.eof())return {};
    return c;
}
inline bool save_weapon_calibration(unsigned weapon,const WeaponCalibration& c,std::string& error) {
    auto path=weapon_calibration_path(weapon);
    if(path.empty()||!valid_weapon_calibration(c)){error="Invalid calibration or profile path.";return false;}
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
    if(ec){error="Cannot create calibration folder.";return false;}
    auto temp=path;temp+=".tmp";
    {std::ofstream out(temp,std::ios::trunc);out.precision(9);out<<1<<'\n';
     for(float v:c.offset)out<<v<<' ';for(float v:c.degrees)out<<v<<' ';out<<c.scale<<'\n';
     out.flush();if(!out){error="Cannot write calibration.";return false;}out.close();if(!out){error="Cannot close calibration file.";return false;}}
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        error="Cannot replace calibration file.";std::filesystem::remove(temp,ec);return false;
    }
#else
    std::filesystem::rename(temp,path,ec);if(ec){error="Cannot replace calibration file.";return false;}
#endif
    error.clear();return true;
}
inline std::array<float,3> rotate_weapon_point(std::array<float,3> p,const XrShotQuat& q) {
    const float tx=2*(q[1]*p[2]-q[2]*p[1]),ty=2*(q[2]*p[0]-q[0]*p[2]),tz=2*(q[0]*p[1]-q[1]*p[0]);
    p[0]+=q[3]*tx+q[1]*tz-q[2]*ty;p[1]+=q[3]*ty+q[2]*tx-q[0]*tz;p[2]+=q[3]*tz+q[0]*ty-q[1]*tx;return p;
}
// Calibrated geometry remains in metres until presentation. A single pose
// packet supplies grip translation and direct aim rotation for the whole eye pair.
struct TrackedWeaponAttachment {
    bool valid=false;
    XrShotQuat aim{0,0,0,1};
    std::array<float,3> grip{};
    std::array<float,3> point(const std::array<float,3>& local) const {
        auto p=rotate_weapon_point(local,aim);
        for(int i=0;i<3;++i)p[i]+=grip[i];
        return p;
    }
};
inline TrackedWeaponAttachment tracked_weapon_attachment(const XrMotionInput& motion) {
    TrackedWeaponAttachment result;
    if(!motion.available || !motion.focused || !motion.right.pose_valid ||
        !motion.right.aim_pose_valid)return result;
    for(int i=0;i<4;++i)result.aim[i]=motion.right.aim_orientation[i];
    if(!xr_normalize_shot_quat(result.aim))return result;
    for(int i=0;i<3;++i) {
        result.grip[i]=motion.right.position[i];
        if(!std::isfinite(result.grip[i]) || std::abs(result.grip[i])>10)return result;
    }
    result.valid=true;return result;
}
inline std::array<float,3> calibrated_weapon_point(std::array<float,3> p,const WeaponCalibration& c) {
    constexpr float radians=0.00872664626F; // degrees to half-angle
    XrShotQuat q{0,0,0,1};
    for(int axis=0;axis<3;++axis){XrShotQuat a{0,0,0,std::cos(c.degrees[axis]*radians)};a[axis]=std::sin(c.degrees[axis]*radians);q=xr_shot_multiply(q,a);}
    for(auto& v:p)v*=c.scale;p=rotate_weapon_point(p,q);
    for(int i=0;i<3;++i)p[i]+=c.offset[i];return p;
}
// Convert extracted mesh points through the same pivot, calibration, aim
// rotation and grip translation used by the visible replacement weapon.
inline std::array<float,3> weapon_mesh_to_tracking(
        const std::array<float,3>& mesh,const std::array<float,4>& placement,
        const WeaponCalibration& calibration,const XrShotQuat& aim,
        const std::array<float,3>& grip) {
    std::array<float,3> p{
        (mesh[0]-placement[1])*placement[0],
        (mesh[2]-placement[3])*placement[0],
        (mesh[1]-placement[2])*placement[0]};
    p=rotate_weapon_point(calibrated_weapon_point(p,calibration),aim);
    for(int i=0;i<3;++i)p[i]+=grip[i];
    return p;
}
}
