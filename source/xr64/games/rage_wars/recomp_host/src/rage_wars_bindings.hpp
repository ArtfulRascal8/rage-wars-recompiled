#pragma once
#include "rage_wars_controller_mapping.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace xr64::rage_wars::bindings {
// Serialized action order is stable; append new actions rather than renumbering.
enum class Action : std::uint8_t {
    Forward, Backward, StrafeLeft, StrafeRight, LookUp, LookDown, LookLeft, LookRight,
    Fire, Jump, SecondaryFire, PreviousWeapon, NextWeapon, Pause, DpadUp, DpadDown, DpadLeft, DpadRight, WeaponWheel, Count
};
enum class Device : std::uint8_t { KeyboardMouse, Controller, XR };
using Code = std::uint16_t;
constexpr unsigned kActionCount = static_cast<unsigned>(Action::Count);
constexpr Code kMouseBase=512, kWheelUp=520, kWheelDown=521, kPadBase=768, kLeftTrigger=800, kRightTrigger=801;
struct Binding { Code primary=0, alternate=0; bool operator==(const Binding&) const = default; };
using Set = std::array<Binding,kActionCount>;
constexpr std::array<const char*,kActionCount> kActionNames = {
    "FORWARD","BACKWARD","STRAFE LEFT","STRAFE RIGHT","LOOK UP","LOOK DOWN","LOOK LEFT","LOOK RIGHT",
    "FIRE","JUMP","SECONDARY FIRE","PREV WEAPON","NEXT WEAPON","PAUSE","D-PAD UP","D-PAD DOWN","D-PAD LEFT","D-PAD RIGHT","WEAPON WHEEL"};
inline Set keyboard_defaults() {
    return {{{26,0},{22,0},{4,0},{7,0},{82,0},{81,0},{80,0},{79,0},
        {513,0},{44,0},{21,515},{20,kWheelUp},{8,kWheelDown},{40,0},{12,0},{14,0},{13,0},{15,0},{43,0}}};
}
inline Set controller_defaults() {
    Set s{};
    s[8]={kRightTrigger,0}; s[9]={kPadBase,0}; s[10]={kPadBase+1,0};
    s[11]={kPadBase+9,0};s[12]={kPadBase+10,0};s[13]={kPadBase+6,0};
    s[14]={kPadBase+11,0};s[15]={kPadBase+12,0};s[16]={kPadBase+13,0};s[17]={kPadBase+14,0};s[18]={kPadBase+2,0};
    return s;
}
constexpr Code kXrBase=1024, kXrMenu=kXrBase+8, kXrLast=kXrBase+16;
inline Set xr_defaults() {
 Set s{};s[0]={kXrBase+9,0};s[1]={kXrBase+10,0};s[2]={kXrBase+11,0};s[3]={kXrBase+12,0};
 s[6]={kXrBase+15,0};s[7]={kXrBase+16,0};
 s[8]={kXrBase+1,0};s[9]={kXrBase+4,0};s[10]={kXrBase+3,kXrBase+5};
 s[11]={kXrBase+6,0};s[12]={kXrBase+7,0};s[13]={kXrMenu,0};return s;
}
inline bool valid_code(Device device, Code code) {
    if(code==0)return true;
    if(device==Device::XR)return code>=kXrBase && code<=kXrLast;
    if(device==Device::KeyboardMouse)
        return (code>=4 && code<512) || (code>=513 && code<=517) || code==kWheelUp || code==kWheelDown;
    return (code>=kPadBase && code<=kPadBase+14) || code==kLeftTrigger || code==kRightTrigger;
}
inline bool reserved_code(Device device, Code code) {
    if(device==Device::XR)return code==kXrMenu;
    if(device==Device::KeyboardMouse) return code==41 || (code>=58 && code<=69) || code==227 || code==231;
    return code==kPadBase+4 || code==kPadBase+5; // Back cancels capture; Guide belongs to the OS.
}
inline Set validate(Set value, Device device) {
    const auto defaults=device==Device::XR ? xr_defaults() : device==Device::Controller ? controller_defaults() : keyboard_defaults();
    for(unsigned i=0;i<kActionCount;++i) {
        if(!valid_code(device,value[i].primary) || reserved_code(device,value[i].primary))value[i].primary=defaults[i].primary;
        if(!valid_code(device,value[i].alternate) || reserved_code(device,value[i].alternate))value[i].alternate=defaults[i].alternate;
    }
    return value;
}
inline void assign(Set& values, unsigned action, unsigned slot, Code code) {
    if(action>=kActionCount || slot>1)return;
    auto& target=slot==0 ? values[action].primary : values[action].alternate;
    const Code old=target;
    if(old==code)return;
    bool swapped=false;
    if(code!=0) for(unsigned i=0;i<kActionCount;++i) for(unsigned j=0;j<2;++j) {
        if(i==action && j==slot)continue;
        auto& candidate=j==0 ? values[i].primary : values[i].alternate;
        if(candidate==code) {candidate=swapped ? 0 : old;swapped=true;}
    }
    target=code;
}
inline std::uint32_t pad_buttons(const recomp::StandardGamepadState& p) {
    std::uint32_t result=p.raw_buttons;
    if(p.south)result|=1U<<0;if(p.east)result|=1U<<1;if(p.start)result|=1U<<6;
    if(p.left_shoulder)result|=1U<<9;if(p.right_shoulder)result|=1U<<10;
    if(p.dpad_up)result|=1U<<11;if(p.dpad_down)result|=1U<<12;
    if(p.dpad_left)result|=1U<<13;if(p.dpad_right)result|=1U<<14;
    return result;
}
inline bool pad_down(const recomp::StandardGamepadState& p, Code code) {
    if(code>=kPadBase && code<=kPadBase+14)return (pad_buttons(p)&(1U<<(code-kPadBase)))!=0;
    if(code==kLeftTrigger)return p.left_trigger>=recomp::kControllerDigitalAxisThreshold;
    if(code==kRightTrigger)return p.right_trigger>=recomp::kControllerDigitalAxisThreshold;
    return false;
}
template<class Down> inline recomp::N64ControllerInput map(const Set& bindings, Down down) {
    std::array<bool,kActionCount> held{};
    for(unsigned i=0;i<kActionCount;++i)
        held[i]=(bindings[i].primary && down(bindings[i].primary)) || (bindings[i].alternate && down(bindings[i].alternate));
    recomp::N64ControllerInput result;
    constexpr std::uint16_t masks[kActionCount] = {8,4,2,1,0,0,0,0,0x2000,0x10,0x4000,0,0,0x1000,0x0800,0x0400,0x0200,0x0100,0x8000};
    for(unsigned i=0;i<kActionCount;++i)if(held[i])result.buttons|=masks[i];
    result.weapon_cycle=std::uint8_t(held[11]) | (std::uint8_t(held[12])<<1);
    result.stick_x=float(held[7])-float(held[6]);
    result.stick_y=float(held[4])-float(held[5]);
    return result;
}
inline std::string name(Device device,Code code) {
    if(code==0)return "UNBOUND";
    if(device==Device::XR) {
        constexpr const char* labels[]={"L TRIGGER","R TRIGGER","L GRIP","R GRIP","A","B","X","Y","MENU","L STICK UP","L STICK DOWN","L STICK LEFT","L STICK RIGHT","R STICK UP","R STICK DOWN","R STICK LEFT","R STICK RIGHT"};
        return code>=kXrBase && code<=kXrLast ? labels[code-kXrBase] : "INVALID";
    }
    if(device==Device::Controller) {
        constexpr const char* buttons[]={"A","B","X","Y","BACK","GUIDE","START","LS CLICK","RS CLICK","LB","RB","DP UP","DP DOWN","DP LEFT","DP RIGHT"};
        if(code>=kPadBase && code<=kPadBase+14)return buttons[code-kPadBase];
        if(code==kLeftTrigger)return "LT";if(code==kRightTrigger)return "RT";
    } else {
        if(code>=4 && code<=29)return std::string(1,char('A'+code-4));
        if(code>=30 && code<=38)return std::string(1,char('1'+code-30));
        if(code==39)return "0";
        if(code>=513 && code<=517)return "MOUSE "+std::to_string(code-512);
        if(code==kWheelUp)return "WHEEL UP";if(code==kWheelDown)return "WHEEL DOWN";
        switch(code) {
        case 40:return "ENTER";case 41:return "ESC";case 42:return "BACKSPACE";case 43:return "TAB";case 44:return "SPACE";
        case 45:return "-";case 46:return "=";case 47:return "[";case 48:return "]";case 49:return "BACKSLASH";
        case 51:return ";";case 52:return "'";case 53:return "GRAVE";case 54:return ",";case 55:return ".";case 56:return "/";
        case 57:return "CAPS LOCK";case 73:return "INSERT";case 74:return "HOME";case 75:return "PAGE UP";
        case 76:return "DELETE";case 77:return "END";case 78:return "PAGE DOWN";
        case 79:return "RIGHT";case 80:return "LEFT";case 81:return "DOWN";case 82:return "UP";
        case 224:return "LCTRL";case 225:return "LSHIFT";case 226:return "LALT";
        case 228:return "RCTRL";case 229:return "RSHIFT";case 230:return "RALT";
        default:break;
        }
    }
    return "KEY "+std::to_string(code);
}
enum class CaptureStage : std::uint8_t { Idle, Release, Listening, Cooldown };
struct Capture {
    Device device=Device::KeyboardMouse;
    unsigned action=0,slot=0;
    CaptureStage stage=CaptureStage::Idle;
    bool reserved=false;
    void start(Device d,unsigned a,unsigned s) {
        device=d;action=a;slot=s;stage=CaptureStage::Release;reserved=false;
    }
    void cancel() { if(stage!=CaptureStage::Idle)stage=CaptureStage::Cooldown;reserved=false; }
    bool poll(bool neutral,bool focused) {
        const auto old=stage;
        if(!focused)cancel();
        if(neutral) {
            if(stage==CaptureStage::Release && focused)stage=CaptureStage::Listening;
            else if(stage==CaptureStage::Cooldown)stage=CaptureStage::Idle;
        }
        return old!=stage;
    }
    bool accept(Device d,Code code) {
        if(stage!=CaptureStage::Listening || d!=device || !code || !valid_code(d,code))return false;
        if(reserved_code(d,code)){reserved=true;return false;}
        stage=CaptureStage::Cooldown;reserved=false;return true;
    }
    bool blocked()const {return stage!=CaptureStage::Idle;}
};
} // namespace xr64::rage_wars::bindings
