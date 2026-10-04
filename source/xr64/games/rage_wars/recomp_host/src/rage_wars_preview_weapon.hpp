#pragma once
#include "rage_wars_xr_guest_input.hpp"
#include "rage_wars_xr_shot.hpp"
#include "rage_wars_weapon_calibration.hpp"
#include "rage_wars_preview_frame.hpp"
#include "xr64_fast3d/n64_raw_fast3d_renderer.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <cstring>
#include <chrono>
namespace xr64::rage_wars::recomp {
// Assets are generated privately from the owner's dumps, never embedded in source.
inline bool load_preview_weapon(const std::filesystem::path& path,
        std::vector<::xr64::N64RawFast3DReplacementBatch>& batches) {
 std::ifstream f(path,std::ios::binary);char magic[4];std::uint32_t version=0,count=0;
 auto read=[&](auto& v){return bool(f.read(reinterpret_cast<char*>(&v),sizeof(v)));};
 if(!f.read(magic,4)||std::memcmp(magic,"RWPM",4)||!read(version)||version!=1||!read(count)||count==0||count>128)return false;
 std::vector<::xr64::N64RawFast3DReplacementBatch> candidate;
 std::size_t total_vertices=0,total_bytes=0;
 for(std::uint32_t i=0;i<count;++i){
  ::xr64::N64RawFast3DReplacementBatch b;std::uint32_t n=0;
  if(!read(n)||!read(b.texture.width)||!read(b.texture.height)||!read(b.combine_word0)||!read(b.combine_word1))return false;
  if(n==0||n%3||n>30000||b.texture.width==0||b.texture.width>1024||b.texture.height==0||b.texture.height>1024)return false;
  total_vertices+=n;total_bytes+=std::size_t(b.texture.width)*b.texture.height*4;
  if(total_vertices>60000||total_bytes>32*1024*1024)return false;
  b.texture.rgba.resize(std::size_t(b.texture.width)*b.texture.height*4);
  if(!f.read(reinterpret_cast<char*>(b.texture.rgba.data()),b.texture.rgba.size()))return false;
  b.vertices.resize(n);
  for(auto& v:b.vertices){
   if(!read(v.x)||!read(v.y)||!read(v.z)||!read(v.s)||!read(v.t))return false;
   for(float x:{v.x,v.y,v.z,v.s,v.t})if(!std::isfinite(x)||std::abs(x)>1e7F)return false;
   v.w=1;v.color={255,255,255,255};
  }
  candidate.push_back(std::move(b));
 }
 if(f.peek()!=std::char_traits<char>::eof())return false;
 batches=std::move(candidate);return true;
}
inline ::xr64::N64RawFast3DListReplacement prepare_preview_weapon(
        const std::uint8_t* ram,std::uint32_t actor,bool xr,const XrMotionInput& motion,
        const PreviewWeaponFrame* completed=nullptr,bool calibrated_models=true) {
 ::xr64::N64RawFast3DListReplacement result;
 if(!calibrated_models)return result; // Original animated guest display list, unchanged.
 const auto directory=weapon_asset_directory();
 if(directory.empty()||!actor||
    xr_guest_context(ram,actor,xr_base_gameplay(ram))!=XrGuestContext::Playing)return result;
 const auto a=actor-0x80000000U,model=a+0x2E8;
 std::uint16_t weapon=0;std::memcpy(&weapon,ram+((a+0x62E)^2U),2);
 if(completed)weapon=static_cast<std::uint16_t>(completed->weapon);
 if(weapon<1||weapon>14)return result; // Chest Burster (15) has no captured asset.
 const auto list=completed?completed->list:controls::guest_word(ram,model+0xC0);
 const auto view=controls::guest_word(ram,a+0x5DC);
 if(!xr_guest_pointer(list,8)||!xr_guest_pointer(view,0x220))return result;
 if(static_cast<std::int32_t>(controls::guest_word(ram,a+0x5E4))<=0)return result;
 static std::map<unsigned,std::vector<::xr64::N64RawFast3DReplacementBatch>> cache;
 auto found=cache.find(weapon);
 if(found==cache.end()) {
  std::vector<::xr64::N64RawFast3DReplacementBatch> batches;
  bool ok=load_preview_weapon(std::filesystem::path(directory)/(std::to_string(weapon)+".rwpm"),batches);
  std::fprintf(stderr,"RW116_PREVIEW_ASSET weapon=%u loaded=%d batches=%zu\n",weapon,ok?1:0,batches.size());
  found=cache.emplace(weapon,std::move(batches)).first;
 }
 if(found->second.empty())return result;
 std::array<float,16> projection{},camera{};
 if(completed)projection=completed->projection;
 else std::memcpy(projection.data(),ram+(view-0x80000000U)+0x1E0,64);
 if(!xr_tracking_to_world(projection,camera))return result;
 XrShotQuat q{0,0,0,1};std::array<float,3> origin{0.18F,-0.20F,-0.40F};
 // Retain calibrated model coordinates; tracking is attached at presentation,
 // using the same predicted frame as the eyes. Decode never freezes a hand pose.
 if(xr)origin={0,0,0};
 for(float v:origin)if(!std::isfinite(v)||std::abs(v)>10)return result;
 float units=100;
 if(const char* setting=std::getenv("XR64_XR_UNITS_PER_METRE")){float v=std::strtof(setting,nullptr);if(std::isfinite(v)&&v>0)units=v;}
 // Initial measured-mesh anchors; physical grip calibration remains a runtime gate.
 static std::map<unsigned,std::array<float,4>> placements;
 auto placement=placements.find(weapon);
 if(placement==placements.end()) {
  std::array<float,4> value{};
  std::ifstream file(std::filesystem::path(directory)/(std::to_string(weapon)+".placement"));
  if(!(file>>value[0]>>value[1]>>value[2]>>value[3]))return result;
  for(float v:value)if(!std::isfinite(v))return result;
  if(value[0]<=0||value[0]>0.01F||std::abs(value[1])>100000||std::abs(value[2])>100000||std::abs(value[3])>100000)return result;
  placement=placements.emplace(weapon,value).first;
 }
 static std::map<unsigned,WeaponCalibration> calibrations;
 auto calibration=calibrations.find(weapon);
 if(calibration==calibrations.end())calibration=calibrations.emplace(weapon,load_weapon_calibration(weapon)).first;
 const auto& place=placement->second;
 const WeaponCalibration model_calibration=xr?calibration->second:WeaponCalibration{};
 result.batches=found->second;result.physical_address=list&0x7FFFFFU;
 result.tracked_attachment=xr;
 for(auto& batch:result.batches)for(auto& v:batch.vertices){
  const auto p=weapon_mesh_to_tracking({v.x,v.y,v.z},place,model_calibration,q,origin);
  if(xr){v.x=p[0];v.y=p[1];v.z=p[2];v.w=1;continue;}
  std::array<float,4> world{camera[12],camera[13],camera[14],1},clip{};
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)world[i]+=p[j]*units*camera[j*4+i];
  for(int i=0;i<4;++i)for(int j=0;j<4;++j)clip[i]+=world[j]*projection[j*4+i];
  v.x=clip[0];v.y=clip[1];v.z=clip[2];v.w=clip[3];
 }
 return result;
}
}
