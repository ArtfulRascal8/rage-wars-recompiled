#pragma once
#include "rage_wars_openxr.hpp"
#include "rage_wars_controls.hpp"
#include <algorithm>
#include <cmath>

namespace xr64::rage_wars::recomp {
inline float xr_binding_value(const XrMotionInput& input,bindings::Code code) {
    const auto i=int(code)-int(bindings::kXrBase);
    switch(i) {
    case 0:return input.left.trigger;case 1:return input.right.trigger;
    case 2:return input.left.grip;case 3:return input.right.grip;
    case 4:return input.right.primary;case 5:return input.right.secondary;
    case 6:return input.left.primary;case 7:return input.left.secondary;case 8:return input.menu_button;
    default:break;
    }
    if(i>=9 && i<=16) {
        const auto& stick=i<13?input.left.stick:input.right.stick;
        const int dir=(i-9)%4;float value=(dir<2?stick[1]:stick[0])*(dir==1||dir==2?-1.0F:1.0F);
        return std::isfinite(value)?std::clamp(value,0.0F,1.0F):0;
    }
    return 0;
}
// Deliberate controls activity only: tracked pose jitter never steals input.
inline controls::DeviceActivity xr_device_activity(const XrMotionInput& input) {
    controls::DeviceActivity out;
    out.available=input.available; out.focused=input.focused;
    for(unsigned i=0;i<9;++i)
        if(xr_binding_value(input,bindings::kXrBase+i)>=0.55F)out.buttons|=1U<<i;
    out.axes[0]=input.left.stick[0];out.axes[1]=input.left.stick[1];
    out.axes[2]=input.right.stick[0];out.axes[3]=input.right.stick[1];
    return out;
}
// The guest still consumes N64 digital movement. This adapter is intentionally
// isolated so continuous locomotion can replace it after the movement hook is known.
class XrMotionMapper {
public:
    N64ControllerInput map(const XrMotionInput& input, bool gameplay, bool /*legacy_probe_ignored*/=false, const controls::Settings& settings=controls::Settings{}) {
        N64ControllerInput out;
        if (!input.available || !input.focused) { movement_=0; return out; }
        if (gameplay) {
            const float threshold=settings.xr_move_threshold;
            update(input.left.stick[1],1,2,threshold,threshold*0.72F,movement_);
            update(input.left.stick[0],8,4,threshold,threshold*0.72F,movement_);
            update(input.right.stick[1],16,32,threshold,threshold*0.72F,movement_);
            update(input.right.stick[0],128,64,threshold,threshold*0.72F,movement_);
            auto down=[&](bindings::Code code) {
                if(code>=bindings::kXrBase+9 && code<=bindings::kXrLast)return bool(movement_&(1U<<(code-bindings::kXrBase-9)));
                return xr_binding_value(input,code)>=settings.xr_trigger_threshold;
            };
            // Preserve the established Touch guest-button contract. Desktop Modern
            // bindings use a different jump/cycle bridge and cannot be reused here.
            constexpr std::uint16_t masks[bindings::kActionCount]={
                kN64ButtonCUp,kN64ButtonCDown,kN64ButtonCLeft,kN64ButtonCRight,0,0,0,0,
                kN64ButtonZ,kN64ButtonA,kN64ButtonB,kN64ButtonL,kN64ButtonR,kN64ButtonStart,
                kN64ButtonDpadUp,kN64ButtonDpadDown,kN64ButtonDpadLeft,kN64ButtonDpadRight,kN64ButtonA};
            for(unsigned action=0;action<bindings::kActionCount;++action) {
                const auto binding=settings.xr_bindings[action];
                if(down(binding.primary)||down(binding.alternate))out.buttons|=masks[action];
            }
            auto strength=[&](unsigned action) {
                const auto binding=settings.xr_bindings[action];
                auto value=[&](bindings::Code code) {
                    float v=xr_binding_value(input,code);
                    if(code>=bindings::kXrBase+9)return turn_axis(v)*2.0F;
                    return down(code)?1.0F:0.0F;
                };
                return std::max(value(binding.primary),value(binding.alternate));
            };
            out.stick_x=(strength(7)-strength(6))*settings.xr_turn_speed;
            out.stick_y=(strength(4)-strength(5))*settings.xr_turn_speed;
            if(!input.right.pose_valid)out.buttons&=~kN64ButtonZ;
            // Always retain a physical pause escape, regardless of gameplay remaps.
            if(input.menu_button)out.buttons|=kN64ButtonStart;
        } else {
            // Menus use the N64 analog stick, so route the left thumbstick there.
            out.stick_x=menu_axis(input.left.stick[0]);
            out.stick_y=menu_axis(input.left.stick[1]);
            if(input.right.primary)out.buttons|=kN64ButtonA;
            if(input.right.secondary)out.buttons|=kN64ButtonB;
            if(input.menu_button)out.buttons|=kN64ButtonStart;
            movement_=0;
        }
        return out;
    }
    static float menu_axis(float x) {
        if(!std::isfinite(x) || std::abs(x)<0.18F)return 0;
        return std::clamp(x,-1.0F,1.0F);
    }
    static float turn_axis(float x) {
        if(!std::isfinite(x))return 0;
        const float magnitude=std::abs(x);
        if(magnitude<=0.12F)return 0;
        return std::copysign((std::min(magnitude,1.0F)-0.12F)/(1.0F-0.12F)*0.5F,x);
    }
private:
    static void update(float axis,std::uint16_t positive,std::uint16_t negative,
                       float press,float release,std::uint16_t& held) {
        if(!std::isfinite(axis))axis=0;
        if(axis>=press || ((held&positive)&&axis>=release))held|=positive;
        else held&=~positive;
        if(axis<=-press || ((held&negative)&&axis<=-release))held|=negative;
        else held&=~negative;
    }
    std::uint16_t movement_=0;
};
}
