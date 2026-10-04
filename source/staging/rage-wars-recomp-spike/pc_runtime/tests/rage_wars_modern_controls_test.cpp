#include "rage_wars_look_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <limits>
using namespace xr64::rage_wars;
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
bool near(float a, float b, float e=0.00001F) { return std::fabs(a-b)<e; }
int main() {
    controls::Settings settings;
    recomp::StandardGamepadState pad{};
    controls::GamepadMapper mapper;
    pad.right_x=2000;
    REQUIRE(mapper.map(pad,settings,true).stick_x==0);
    float previous=0;
    for(int raw=2622;raw<=32767;raw+=317) {
        pad.right_x=static_cast<std::int16_t>(raw);
        float current=mapper.map(pad,settings,true).stick_x;
        REQUIRE(current>=previous); previous=current;
    }
    pad.right_x=32767; pad.right_y=-32768;
    auto input=mapper.map(pad,settings,true);
    REQUIRE(near(std::hypot(input.stick_x,input.stick_y),1));
    pad={}; pad.right_x=16384;
    const float linear=mapper.map(pad,settings,true).stick_x;
    settings.response_curve=controls::ResponseCurve::Precision;
    REQUIRE(mapper.map(pad,settings,true).stick_x<linear);
    settings.profile=controls::Profile::Classic;
    REQUIRE(near(mapper.map(pad,settings,true).stick_x,recomp::map_standard_gamepad(pad).stick_x));
    settings={}; pad={};pad.left_x=7000;
    REQUIRE(mapper.map(pad,settings,true).buttons&recomp::kN64ButtonCRight);
    pad.left_x=6000;
    REQUIRE(mapper.map(pad,settings,true).buttons&recomp::kN64ButtonCRight);
    pad.left_x=4500;
    REQUIRE(!(mapper.map(pad,settings,true).buttons&recomp::kN64ButtonCRight));
    pad.left_x=-9000;
    input=mapper.map(pad,settings,true);
    REQUIRE(input.buttons&recomp::kN64ButtonCLeft);
    REQUIRE(!(input.buttons&recomp::kN64ButtonCRight));
    controls::DeviceSelector selector;
    using controls::Device;using controls::DeviceMode;
    pad={};pad.right_x=17000;
    REQUIRE(selector.update(DeviceMode::Auto,true,pad,false,true)==Device::Controller);
    REQUIRE(selector.update(DeviceMode::Auto,true,pad,true,true)==Device::KeyboardMouse);
    for(int i=0;i<100;++i) {
        pad.right_x=17000+i%50;
        REQUIRE(selector.update(DeviceMode::Auto,true,pad,false,true)==Device::KeyboardMouse);
    }
    pad.south=true;
    REQUIRE(selector.update(DeviceMode::Auto,true,pad,false,true)==Device::Controller);
    REQUIRE(selector.update(DeviceMode::Auto,false,pad,false,true)==Device::KeyboardMouse);
    REQUIRE(selector.update(DeviceMode::Controller,false,pad,true,true)==Device::KeyboardMouse);
    controls::DeviceActivity touch;touch.available=touch.focused=true;touch.buttons=1;
    REQUIRE(selector.update(DeviceMode::Auto,false,pad,false,true,touch)==Device::Touch);
    REQUIRE(selector.update(DeviceMode::Auto,false,pad,true,true,touch)==Device::KeyboardMouse);
    // Held Touch buttons/pose motion do not steal ownership from the keyboard.
    REQUIRE(selector.update(DeviceMode::Auto,false,pad,false,true,touch)==Device::KeyboardMouse);
    touch.buttons=3;
    REQUIRE(selector.update(DeviceMode::Auto,false,pad,false,false,touch)==Device::Touch);
    touch.focused=false;
    REQUIRE(selector.update(DeviceMode::Auto,false,pad,false,true,touch)==Device::KeyboardMouse);
    recomp::KeyboardMouseState menu_keys{};menu_keys.move_right=true;menu_keys.mouse_x=-500;
    REQUIRE(recomp::map_keyboard_menu(menu_keys).stick_x==1);
    menu_keys={};menu_keys.dpad_left=true;menu_keys.mouse_x=500;
    REQUIRE(recomp::map_keyboard_menu(menu_keys).stick_x==-1);
    menu_keys={};menu_keys.mouse_x=500;menu_keys.mouse_y=-900;
    REQUIRE(recomp::map_keyboard_menu(menu_keys).stick_x==0 && recomp::map_keyboard_menu(menu_keys).stick_y==0);
    auto keyboard=recomp::N64ControllerInput{}, gamepad=keyboard;
    keyboard.buttons=recomp::kN64ButtonStart|recomp::kN64ButtonZ;
    gamepad.buttons=recomp::kN64ButtonA;
    REQUIRE(controls::route_input(Device::Controller,gamepad,keyboard,true,true).buttons==
        (recomp::kN64ButtonA|recomp::kN64ButtonStart));
    REQUIRE(controls::route_input(Device::Controller,gamepad,keyboard,true,false).buttons==0);
    REQUIRE(controls::route_input(Device::Controller,gamepad,keyboard,false,true).buttons==
        (gamepad.buttons|keyboard.buttons));
    controls::MouseMotion mouse;
    for(int i=0;i<1000;++i) mouse.add(23,-17);
    auto motion=mouse.take();
    REQUIRE(motion.x==23000 && motion.y==-17000);
    REQUIRE(mouse.take().x==0);
    mouse.add(900,400);mouse.clear();REQUIRE(mouse.take().y==0);
    controls::LookSample sample;sample.focused=true;sample.mouse_x=100;sample.mouse_y=-50;
    const auto slow=controls::look_delta(sample,settings,0.5F),fast=controls::look_delta(sample,settings,0.25F);
    REQUIRE(near(slow.yaw,fast.yaw) && near(slow.pitch,fast.pitch));
    REQUIRE(slow.yaw>0 && slow.pitch>0);
    settings.controller_invert_y=true;
    REQUIRE(near(controls::look_delta(sample,settings,0.5F).pitch,slow.pitch));
    settings.mouse_invert_y=true;
    REQUIRE(near(controls::look_delta(sample,settings,0.5F).pitch,-slow.pitch));
    settings={}; sample.device=Device::Controller;sample.stick={0.5F,0.5F};
    REQUIRE(near(controls::look_delta(sample,settings,0.5F).yaw,
        controls::look_delta(sample,settings,0.25F).yaw*2));
    REQUIRE(controls::look_delta(sample,settings,0).yaw==0);
    sample.focused=false;REQUIRE(controls::look_delta(sample,settings,0.5F).pitch==0);
    settings.aim_deadzone=std::numeric_limits<float>::quiet_NaN();
    REQUIRE(controls::validate(settings).aim_deadzone==0.08F);
    settings={}; sample.focused=true;
    std::vector<std::uint8_t> storage(0x800000);auto* ram=storage.data();
    constexpr std::uint32_t actor=0x80200000,view=0x80210000,config=0x800C94B4;
    auto word=[&](std::uint32_t off,std::uint32_t value){std::memcpy(ram+off,&value,4);};
    word(0x200000+0x698,0x80109328);
    word(0x200000+0x5DC,view);word(0x200000+0x5E4,100);
    ram[0x140225^3]=1;
    controls::guest_float_write(ram,0xCD648,0.5F);
    controls::guest_float_write(ram,0xC94B4,0.25F);
    controls::guest_float_write(ram,0xC94C4,-1.570796F);
    controls::guest_float_write(ram,0xC94C8,1.570796F);
    controls::LookFrame frame;
    word(0x200000+0x698,0x80109400);frame.begin(ram,actor,sample,settings);
    REQUIRE(!frame.apply(ram,actor,actor,config,false));
    word(0x200000+0x698,0x80109328);frame.begin(ram,actor,sample,settings);
    REQUIRE(!frame.apply(ram,actor,view,config,false)); // native owner argument is the actor
    REQUIRE(!frame.apply(ram,actor+4,actor+4,config,false));
    REQUIRE(frame.apply(ram,actor,actor,config,false));
    const float yaw=controls::guest_float(ram,0x20006C);
    REQUIRE(yaw>0);
    REQUIRE(frame.apply(ram,actor,actor,config,false));
    REQUIRE(controls::guest_float(ram,0x20006C)==yaw);
    REQUIRE(frame.apply(ram,actor,actor,config,true));
    REQUIRE(controls::guest_float(ram,0x200724)>0);
    frame.begin(ram,actor,sample,settings);word(0x1407D4,1);
    REQUIRE(!frame.apply(ram,actor,actor,config,false));
    word(0x1407D4,0);word(0x2005E4,0);
    REQUIRE(!frame.apply(ram,actor,actor,config,false));
    word(0x2005E4,100);word(0x2005D4,1);
    REQUIRE(!frame.apply(ram,actor,actor,config,false));
    word(0x2005D4,0);
    settings.profile=controls::Profile::Classic;frame.begin(ram,actor,sample,settings);
    REQUIRE(!frame.apply(ram,actor,actor,config,false));
    // Gameplay bindings can change without changing the native navigation layout.
    settings={};pad={};pad.south=true;pad.raw_buttons=1;
    bindings::assign(settings.controller_bindings,8,0,bindings::kPadBase);
    REQUIRE(settings.controller_bindings[9].primary==bindings::kRightTrigger);
    REQUIRE(mapper.map(pad,settings,true).buttons==recomp::kN64ButtonZ);
    REQUIRE(mapper.map(pad,settings,false).buttons==recomp::kN64ButtonA);
    bindings::assign(settings.controller_bindings,8,1,bindings::kLeftTrigger);
    pad={};pad.left_trigger=20000;
    REQUIRE(mapper.map(pad,settings,true).buttons==recomp::kN64ButtonZ);
    bindings::assign(settings.controller_bindings,8,1,0);
    REQUIRE(mapper.map(pad,settings,true).buttons==0);
    settings={};pad={};settings.swap_sticks=true;pad.left_x=32767;
    REQUIRE(mapper.map(pad,settings,true).stick_x==1);
    REQUIRE(!(mapper.map(pad,settings,true).buttons&recomp::kN64ButtonCRight));
    pad={};pad.right_y=-32768;
    REQUIRE(mapper.map(pad,settings,true).buttons&recomp::kN64ButtonCUp);
    auto keys=bindings::keyboard_defaults();
    bindings::assign(keys,8,0,26);
    REQUIRE(keys[0].primary==513);
    REQUIRE(bindings::map(keys,[](bindings::Code c){return c==26;}).buttons==recomp::kN64ButtonZ);
    REQUIRE(bindings::map(keys,[](bindings::Code c){return c==513;}).buttons==recomp::kN64ButtonCUp);
    REQUIRE(bindings::map(keys,[](bindings::Code c){return c==bindings::kWheelDown;}).weapon_cycle==2);
    REQUIRE(bindings::controller_defaults()[8].primary==bindings::kRightTrigger);
    keys[8].primary=65535;keys[9].primary=41;
    keys=bindings::validate(keys,bindings::Device::KeyboardMouse);
    REQUIRE(keys[8].primary==513 && keys[9].primary==44);
    bindings::Capture capture;
    using BDevice=bindings::Device;using Stage=bindings::CaptureStage;
    capture.start(BDevice::Controller,8,1);
    REQUIRE(capture.blocked() && !capture.accept(BDevice::Controller,bindings::kPadBase));
    REQUIRE(!capture.poll(false,true));
    REQUIRE(capture.poll(true,true) && capture.stage==Stage::Listening);
    REQUIRE(!capture.accept(BDevice::KeyboardMouse,26));
    REQUIRE(!capture.accept(BDevice::Controller,bindings::kPadBase+5) && capture.reserved);
    REQUIRE(capture.accept(BDevice::Controller,bindings::kLeftTrigger));
    REQUIRE(capture.stage==Stage::Cooldown && capture.blocked());
    REQUIRE(!capture.accept(BDevice::Controller,bindings::kPadBase));
    capture.poll(false,true);REQUIRE(capture.blocked());
    capture.poll(true,true);REQUIRE(!capture.blocked());
    capture.start(BDevice::KeyboardMouse,9,0);capture.poll(true,true);
    capture.poll(false,false);REQUIRE(capture.stage==Stage::Cooldown);
    capture.poll(true,false);REQUIRE(!capture.blocked());
    settings={};word(0x200698,0x80109328);
    REQUIRE(controls::spring_value(ram,actor,1,settings)==0);
    settings.look_spring=true;REQUIRE(controls::spring_value(ram,actor,0,settings)==1);
    word(0x2005D4,1);REQUIRE(controls::spring_value(ram,actor,0,settings)==0);
    word(0x2005D4,0);word(0x200698,0x80109500);
    REQUIRE(controls::spring_value(ram,actor,0,settings)==0);
    REQUIRE(controls::spring_value(ram,0,1,settings)==1);
    frame.begin(ram,actor,sample,settings);
    REQUIRE(!frame.apply(ram,actor,actor,config,true));
    std::puts("PASS bindings: swaps, alternates, wheel, sticks, menus, capture lifecycle, local spring");
    std::puts("PASS response, hysteresis, device arbitration, input isolation, mouse conservation, timestep, inversion, local-player guards");
}
