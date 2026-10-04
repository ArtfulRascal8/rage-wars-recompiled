#pragma once
#include "rage_wars_look_policy.hpp"
#include <cstdint>
namespace xr64::rage_wars::recomp {
enum class XrGuestContext { Unknown, Menu, Team, Playing };
inline bool xr_guest_pointer(std::uint32_t p,std::uint32_t bytes) {
    return p>=0x80000000U && p<=0x80800000U-bytes && !(p&3U);
}
// Same guest-owned base state as RW105 input routing. Attract actors can
// have controller zero and a valid camera while the game flag remains zero.
inline bool xr_base_gameplay(const std::uint8_t* ram) {
    return ram && ram[0x140225U^3U]==1 &&
        controls::guest_word(ram,0x1407D4U)==0 &&
        controls::guest_word(ram,0x144E00U)==0;
}
// Called on the guest thread with the actor supplied by its real update.
// No heap scans, A-button inference, or guest memory writes.
inline XrGuestContext xr_guest_context(const std::uint8_t* ram,std::uint32_t actor,bool base_gameplay) {
    using controls::guest_word;
    if(!base_gameplay) return XrGuestContext::Menu;
    if(!ram || !controls::local_actor(ram,actor) ||
       guest_word(ram,actor-0x80000000U+0x698)!=0x80109328U)
        return XrGuestContext::Unknown;
    const auto a=actor-0x80000000U;
    const auto profile=guest_word(ram,a+0x5D8),view=guest_word(ram,a+0x5DC);
    if(!xr_guest_pointer(profile,0x94) || !xr_guest_pointer(view,0x134))
        return XrGuestContext::Unknown;
    const auto team_state=guest_word(ram,a+0xCCC);
    if(team_state>3) return XrGuestContext::Unknown;
    if(team_state!=0 || (guest_word(ram,0x140804)!=0 &&
          ram[((profile-0x80000000U)+0x92)^3U]==0xFF))
        return XrGuestContext::Team;
    return XrGuestContext::Playing;
}
bool gate5_xr_guest_gameplay(const std::uint8_t* ram,bool base_gameplay);
}
