#include "rage_wars_xr_controls.hpp"
#include <cassert>
#include <cmath>
using namespace xr64::rage_wars::recomp;
int main() {
    XrMotionMapper mapper;
    XrMotionInput input;
    input.available=input.focused=true;
    input.right.pose_valid=true;

    // The guest menu consumes its analog stick: Touch left-stick up/down must reach it.
    input.left.stick[1]=0.8F;
    input.right.primary=true;
    auto out=mapper.map(input,false);
    assert(out.stick_y>0.7F && out.stick_x==0);
    assert(out.buttons==kN64ButtonA);
    input.left.stick[1]=-0.8F;
    input.right.primary=false;
    input.right.secondary=true;
    input.right.trigger=1.0F;
    out=mapper.map(input,false);
    assert(out.stick_y<-0.7F && out.buttons==kN64ButtonB);
    input.left.stick[1]=0.1F;
    assert(mapper.map(input,false).stick_y==0);
    input.right.secondary=false;
    input.left.primary=true;
    assert(mapper.map(input,false).buttons==0); // X is no longer Start.
    input.menu_button=true;
    assert(mapper.map(input,false).buttons==kN64ButtonStart);
    input.menu_button=false;
    input.left.primary=false;

    input.right.secondary=false;
    input.right.primary=false;
    input.right.grip=0.8F;
    assert(mapper.map(input,true).buttons&kN64ButtonB);
    input.right.grip=0;
    assert(!(mapper.map(input,true).buttons&kN64ButtonB));
    input.right.primary=true;
    input.right.grip=0.8F;
    input.menu_button=true;
    input.left.primary=true;
    input.left.secondary=true;
    input.left.stick[1]=0.26F;
    out=mapper.map(input,true);
    assert((out.buttons&(kN64ButtonCUp|kN64ButtonZ|kN64ButtonR|
                         kN64ButtonA|kN64ButtonB|kN64ButtonStart|kN64ButtonL))==
           (kN64ButtonCUp|kN64ButtonZ|kN64ButtonR|
            kN64ButtonA|kN64ButtonB|kN64ButtonStart|kN64ButtonL));
    input.left.stick[1]=0.19F;
    assert(mapper.map(input,true).buttons&kN64ButtonCUp);
    input.left.stick[1]=0.17F;
    assert(!(mapper.map(input,true).buttons&kN64ButtonCUp));
    input.right.pose_valid=false;
    assert(!(mapper.map(input,true).buttons&kN64ButtonZ));
    assert(mapper.map(input,true).buttons&kN64ButtonR);

    input.left.stick[0]=0.9F;
    input.left.stick[1]=-0.9F;
    input.right.pose_valid=true;
    input.right.primary=false;
    // The caller supplies guest-observed gameplay; A cannot change it.
    out=mapper.map(input,false,true);
    assert(out.stick_x>0.8F && out.stick_y<-0.8F);
    assert((out.buttons&(kN64ButtonCUp|kN64ButtonCDown|kN64ButtonCLeft|kN64ButtonCRight))==0);
    input.right.primary=true;
    out=mapper.map(input,false,true);
    assert(out.buttons&kN64ButtonA);
    input.right.primary=false;
    out=mapper.map(input,false,true);
    assert(out.stick_y<-0.8F); // Failed/early confirmation keeps navigation.
    out=mapper.map(input,true,true); // Only an actual guest context change resumes movement.
    assert((out.buttons&(kN64ButtonCRight|kN64ButtonCDown))==(kN64ButtonCRight|kN64ButtonCDown));
    assert(out.stick_x==0 && out.stick_y==0);
    out=mapper.map(input,false,true); // Reopened team overlay.
    assert(out.stick_y<-0.8F);
    input.left.stick[0]=input.left.stick[1]=0;
    input.focused=false;
    assert(mapper.map(input,true).buttons==0);
    input.focused=true;
    input.right.stick[0]=1;
    out=mapper.map(input,true);
    assert(std::abs(out.stick_x-0.5F)<0.0001F && out.stick_y==0);
    auto settings=xr64::rage_wars::controls::Settings{};
    settings.xr_bindings[8]={xr64::rage_wars::bindings::kXrBase+2,0};
    input={};input.available=input.focused=input.right.pose_valid=true;
    input.left.grip=0.8F;
    if(!(mapper.map(input,true,false,settings).buttons&kN64ButtonZ))return 1;
    input.left.grip=0;input.right.trigger=0.9F;
    if(mapper.map(input,true,false,settings).buttons&kN64ButtonZ)return 2;
    settings.xr_bindings={};input.right.primary=true;input.left.stick[1]=1;
    auto menu=mapper.map(input,false,false,settings);
    if(!(menu.buttons&kN64ButtonA) || menu.stick_y!=1)return 3;
    input.menu_button=true;
    if(!(mapper.map(input,true,false,settings).buttons&kN64ButtonStart))return 4;
    auto c=xr64::rage_wars::bindings::Capture{};
    c.start(xr64::rage_wars::bindings::Device::XR,8,0);
    if(c.accept(xr64::rage_wars::bindings::Device::XR,xr64::rage_wars::bindings::kXrBase))return 5;
    c.poll(true,true);
    if(c.accept(xr64::rage_wars::bindings::Device::XR,xr64::rage_wars::bindings::kXrMenu))return 6;
    if(!c.accept(xr64::rage_wars::bindings::Device::XR,xr64::rage_wars::bindings::kXrBase+2))return 7;
    if(!c.blocked())return 8;c.poll(true,true);if(c.blocked())return 9;
}
