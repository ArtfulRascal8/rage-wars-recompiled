#include "rage_wars_port_options.hpp"
#include "rage_wars_native_pc_option_ids.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
extern "C" const char* xr64_pc_option_label(std::uint32_t);
extern "C" void xr64_pc_option_cycle(std::uint32_t);
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
int main(int argc,char** argv) {
    REQUIRE(argc==2 && std::getenv("XR64_PORT_OPTIONS_CONFIG"));
    using namespace xr64::rage_wars;
    if(std::string(argv[1])=="legacy") {
        {std::ofstream file(std::getenv("XR64_PORT_OPTIONS_CONFIG"));
         file << R"({"version":1,"mouseInvertY":true,"fullscreen":true})";}
        const auto old=port_options::snapshot();
        REQUIRE(old.controls.xr_bindings==bindings::xr_defaults());
        REQUIRE(old.controls.xr_motion_controls);
        REQUIRE(old.calibrated_weapon_models);
        REQUIRE(old.fullscreen && old.controls.mouse_invert_y);
        REQUIRE(!old.controls.look_spring && !old.controls.swap_sticks);
        REQUIRE(old.controls.keyboard_bindings==bindings::keyboard_defaults());
        REQUIRE(old.controls.controller_bindings==bindings::controller_defaults());
        std::puts("PASS legacy options migration");return 0;
    }
    if(std::string(argv[1])=="write") {
        port_options::reset_to_defaults();
        auto s=port_options::snapshot();
        s.master_volume=0.7F;
        s.calibrated_weapon_models=false;
        s.controls.xr_motion_controls=false;
        s.controls.xr_turn_speed=0.8F;s.controls.xr_move_threshold=0.4F;s.controls.xr_trigger_threshold=0.7F;
        s.controls.xr_bindings[8]={bindings::kXrBase+2,bindings::kXrBase};
        s.controls.device_mode=controls::DeviceMode::Controller;
        s.controls.response_curve=controls::ResponseCurve::Precision;
        s.controls.controller_invert_y=true;
        s.controls.mouse_invert_x=true;
        s.controls.mouse_sensitivity_x=1.75F;
        s.controls.aim_deadzone=0.12F;
        s.controls.movement_threshold=0.35F;
        s.controls.look_spring=true;s.controls.swap_sticks=true;
        for(unsigned i=0;i<bindings::kActionCount;++i) {
            s.controls.keyboard_bindings[i]={static_cast<bindings::Code>(100+i),static_cast<bindings::Code>(140+i)};
            s.controls.controller_bindings[i]={bindings::kPadBase+2,bindings::kLeftTrigger};
        }
        port_options::replace(s);
        port_options::select_binding(bindings::Device::Controller,8);
        port_options::start_binding_capture(0);port_options::poll_binding_capture(true,true);
        const auto revision=port_options::revision();
        REQUIRE(port_options::capture_binding_input(bindings::Device::Controller,bindings::kRightTrigger));
        REQUIRE(port_options::revision()>revision && port_options::binding_input_blocked());
        port_options::poll_binding_capture(true,true);
        REQUIRE(!port_options::binding_input_blocked());
    } else {
        const auto s=port_options::snapshot();
        REQUIRE(s.master_volume==0.7F);
        REQUIRE(!s.calibrated_weapon_models);
        REQUIRE(std::string(xr64_pc_option_label(XR64_PC_WEAPON_MODELS))=="MODELS: ORIGINAL");
        xr64_pc_option_cycle(XR64_PC_WEAPON_MODELS);
        REQUIRE(port_options::snapshot().calibrated_weapon_models);
        REQUIRE(std::string(xr64_pc_option_label(XR64_PC_WEAPON_MODELS))=="MODELS: CALIBRATED");
        REQUIRE(!s.controls.xr_motion_controls);
        xr64_pc_option_cycle(XR64_PC_XR_MOTION);
        REQUIRE(port_options::snapshot().controls.xr_motion_controls);
        REQUIRE(s.controls.xr_turn_speed==0.8F && s.controls.xr_move_threshold==0.4F && s.controls.xr_trigger_threshold==0.7F);
        REQUIRE(s.controls.xr_bindings[8].primary==bindings::kXrBase+2 && s.controls.xr_bindings[8].alternate==bindings::kXrBase);
        xr64_pc_option_cycle(XR64_PC_XR_RESET);
        REQUIRE(port_options::snapshot().controls.xr_bindings==bindings::xr_defaults());
        REQUIRE(port_options::snapshot().controls.keyboard_bindings==s.controls.keyboard_bindings);
        REQUIRE(s.controls.look_spring && s.controls.swap_sticks);
        for(unsigned i=0;i<bindings::kActionCount;++i) {
            REQUIRE(s.controls.keyboard_bindings[i].primary==100+i);
            REQUIRE(s.controls.keyboard_bindings[i].alternate==140+i);
            REQUIRE(s.controls.controller_bindings[i].primary==(i==8 ? bindings::kRightTrigger : bindings::kPadBase+2));
            REQUIRE(s.controls.controller_bindings[i].alternate==bindings::kLeftTrigger);
        }
        REQUIRE(s.controls.device_mode==controls::DeviceMode::Controller);
        REQUIRE(s.controls.response_curve==controls::ResponseCurve::Precision);
        REQUIRE(s.controls.controller_invert_y && !s.controls.mouse_invert_y);
        REQUIRE(s.controls.mouse_invert_x && !s.controls.controller_invert_x);
        REQUIRE(s.controls.mouse_sensitivity_x==1.75F);
        REQUIRE(s.controls.aim_deadzone==0.12F && s.controls.movement_threshold==0.35F);
        for(unsigned id=0;id<=XR64_PC_MOUSE_RESET;++id) {
            REQUIRE(std::string(xr64_pc_option_label(id)).size()<32);
        }
        xr64_pc_option_cycle(XR64_PC_PAD_RESET);
        REQUIRE(!port_options::snapshot().controls.controller_invert_y);
        REQUIRE(!port_options::snapshot().controls.swap_sticks);
        REQUIRE(port_options::snapshot().controls.controller_bindings==bindings::controller_defaults());
        REQUIRE(port_options::snapshot().controls.keyboard_bindings[8].primary==108);
        xr64_pc_option_cycle(XR64_PC_LOOK_SPRING);
        REQUIRE(!port_options::snapshot().controls.look_spring);
        xr64_pc_option_cycle(XR64_PC_RESET_KEYS);
        REQUIRE(port_options::snapshot().controls.keyboard_bindings==bindings::keyboard_defaults());
        REQUIRE(port_options::snapshot().controls.mouse_invert_x);
        xr64_pc_option_cycle(XR64_PC_MOUSE_RESET);
        REQUIRE(port_options::snapshot().controls.mouse_sensitivity_x==1.0F);
    }
    std::puts("PASS persisted controls");
}
