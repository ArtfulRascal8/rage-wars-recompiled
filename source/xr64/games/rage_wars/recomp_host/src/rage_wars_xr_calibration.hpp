#pragma once
#include <array>
#include <cmath>

namespace xr64::rage_wars::recomp {
// Reference-space events take effect at their runtime time, not on receipt.
// A display prediction may cross a reset before a current-time shot sample;
// that sample must wait until it belongs to the same space epoch.
struct XrReferenceEpoch {
    unsigned long long epoch=1;
    long long not_before=0;
    std::array<long long,8> pending{};
    unsigned count=0;
    bool overflow=false;
    void schedule(long long time) {
        if(time<=0)return;
        if(count==pending.size()){overflow=true;return;}
        pending[count++]=time;
    }
    bool advance(long long target,bool manual=false) {
        bool changed=overflow;
        if(overflow){not_before=target;count=0;overflow=false;}
        unsigned kept=0;
        for(unsigned i=0;i<count;++i) {
            if(target>=pending[i]){changed=true;if(pending[i]>not_before)not_before=pending[i];}
            else pending[kept++]=pending[i];
        }
        count=kept;
        if(changed || manual){++epoch;return true;}
        return false;
    }
    void clear(){count=0;overflow=false;not_before=0;++epoch;}
};
// Keep the game horizon level even if the headset is tilted when recentering.
inline std::array<float, 4> xr_level_yaw_origin(const std::array<float, 4>& q) {
    const float x=q[0], y=q[1], z=q[2], w=q[3];
    const float forward_x=-2.0F*(x*z+w*y);
    const float forward_z=-1.0F+2.0F*(x*x+y*y);
    const float horizontal=std::hypot(forward_x,forward_z);
    if (!std::isfinite(horizontal) || horizontal<0.0001F) return {0,0,0,1};
    const float yaw=std::atan2(-forward_x,-forward_z);
    return {0,std::sin(yaw*0.5F),0,std::cos(yaw*0.5F)};
}
}
