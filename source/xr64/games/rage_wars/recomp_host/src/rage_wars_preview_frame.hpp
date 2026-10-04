#pragma once
#include "rage_wars_local_presentation.hpp"
#include <array>
#include <cstdint>
namespace xr64::rage_wars::recomp {
// Captured after the native first-person draw, before model+C0 is cleared.
struct PreviewWeaponFrame {
 std::uint32_t actor=0,list=0,weapon=0;
 std::array<float,16> projection{};
 std::int64_t recorded_ns=0;
 LocalPlayerPresentation player{};
};
inline PreviewWeaponFrame select_preview_weapon_frame(
 const std::array<PreviewWeaponFrame,2>& frames,std::uint32_t task_address,
 std::uint32_t task_size,std::int64_t now) {
 const std::uint64_t begin=task_address&0x1FFFFFFFU,end=begin+task_size;
 if(!task_size||end>0x800000)return {};
 for(const auto& frame:frames) {
  const auto physical=frame.list&0x1FFFFFFFU;
  const auto age=now-frame.recorded_ns;
  if(frame.actor && age>=0 && age<=250'000'000LL && physical>=begin && physical<end)return frame;
 }
 return {};
}
PreviewWeaponFrame gate5_preview_weapon_frame(std::uint32_t task_address,std::uint32_t task_size);
}
