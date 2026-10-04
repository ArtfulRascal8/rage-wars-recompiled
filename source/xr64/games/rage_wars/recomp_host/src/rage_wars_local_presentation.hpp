#pragma once
#include "rage_wars_look_policy.hpp"
#include "rage_wars_control_sample.hpp"
#include "rage_wars_local_telemetry.hpp"
#include "rage_wars_xr_shot.hpp"
#include "xr64_fast3d/n64_raw_fast3d_renderer.hpp"
#include <mutex>

namespace xr64::rage_wars::recomp {
struct LocalMouseStamp {
    std::uint64_t epoch=1, sequence=0;
    double yaw=0, pitch=0;
};
struct LocalPlayerPresentation {
    bool valid=false,allow_yaw=true,allow_pitch=true;
    std::uint32_t actor=0;
    std::uint64_t update=0, continuity=0;
    std::int64_t sampled_ns=0, update_begin_ns=0, update_period_ns=0, update_work_ns=0;
    std::array<float,3> origin{};
    std::array<float,16> projection{};
    float yaw=0, pitch=0, pitch_low=-1.570796F, pitch_high=1.570796F;
    LocalMouseStamp mouse{};
};
struct LocalPresentationState {
    LocalPlayerPresentation player{};
    LocalMouseStamp mouse{};
    bool mouse_enabled=false;
    std::uint64_t update_begins=0, look_checks=0, look_errors=0;
};
// Input is angular displacement. The guest consumes one interval; each scene
// records the consumed endpoint. Replay uses the difference to that endpoint,
// so publishing an older scene cannot erase input or apply an interval twice.
class LocalPresentationMailbox {
    mutable std::mutex mutex_;
    LocalPresentationState state_{};
    LocalMouseStamp taken_{}, applied_{};
    std::int64_t update_begin_ns_=0;
public:
    void update_begin(std::int64_t now) {
        std::lock_guard lock(mutex_);update_begin_ns_=now;++state_.update_begins;
    }
    void check_look(bool correct) {
        if(!control_sample_enabled() && !local_state_sample_enabled())return;
        std::lock_guard lock(mutex_);++state_.look_checks;if(!correct)++state_.look_errors;
    }
    void mouse_context(bool enabled) {
        std::lock_guard lock(mutex_);
        if(enabled==state_.mouse_enabled) return;
        state_.mouse_enabled=enabled;
        const auto epoch=state_.mouse.epoch+1;
        state_.mouse={epoch,0,0,0};taken_=applied_=state_.mouse;
    }
    void add_mouse(double x,double y,const controls::Settings& settings) {
        if(!std::isfinite(x)||!std::isfinite(y)||(x==0 && y==0))return;
        controls::LookSample sample;sample.focused=true;sample.mouse_x=x;sample.mouse_y=y;
        const auto delta=controls::look_delta(sample,settings,0);
        std::lock_guard lock(mutex_);
        if(!state_.mouse_enabled)return;
        state_.mouse.yaw+=delta.yaw;
        if(!settings.look_spring)state_.mouse.pitch+=delta.pitch;
        ++state_.mouse.sequence;
    }
    controls::LookSample take_mouse() {
        std::lock_guard lock(mutex_);
        controls::LookSample sample;
        sample.mouse_epoch=state_.mouse.epoch;sample.mouse_sequence=state_.mouse.sequence;
        sample.mouse_yaw_total=state_.mouse.yaw;sample.mouse_pitch_total=state_.mouse.pitch;
        sample.mouse_yaw=state_.mouse.yaw-taken_.yaw;
        sample.mouse_pitch=state_.mouse.pitch-taken_.pitch;
        taken_=state_.mouse;
        return sample;
    }
    void applied_mouse(const controls::LookSample& sample,bool pitch) {
        std::lock_guard lock(mutex_);
        if(sample.mouse_epoch!=state_.mouse.epoch)return;
        applied_.epoch=sample.mouse_epoch;applied_.sequence=sample.mouse_sequence;
        if(pitch)applied_.pitch=sample.mouse_pitch_total;
        else applied_.yaw=sample.mouse_yaw_total;
    }
    LocalPlayerPresentation capture(LocalPlayerPresentation player) const {
        std::lock_guard lock(mutex_);
        player.mouse=applied_;
        player.continuity=state_.player.continuity;
        player.update=state_.player.update;
        return player;
    }
    void publish(LocalPlayerPresentation player) {
        std::lock_guard lock(mutex_);
        const auto& old=state_.player;
        player.mouse=applied_;
        player.update=old.update+1;
        player.update_begin_ns=update_begin_ns_;
        player.update_work_ns=player.sampled_ns-update_begin_ns_;
        player.update_period_ns=old.actor==player.actor && old.sampled_ns>0 ? player.sampled_ns-old.sampled_ns : 0;
        player.continuity=old.continuity;
        float distance2=0;
        for(int i=0;i<3;++i)distance2+=(player.origin[i]-old.origin[i])*(player.origin[i]-old.origin[i]);
        // Discontinuities invalidate retained-scene corrections until a scene
        // from the new origin arrives. No interpolation across a reset.
        if(!old.valid || !player.valid || player.actor!=old.actor || distance2>10000.0F)
            ++player.continuity;
        state_.player=player;
    }
    LocalPresentationState acquire() const {
        std::lock_guard lock(mutex_);return state_;
    }
};
LocalPresentationMailbox& local_presentation();

inline std::array<float,3> rotate_axis(std::array<float,3> v,
        const std::array<float,3>& axis,float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    const float dot=v[0]*axis[0]+v[1]*axis[1]+v[2]*axis[2];
    const std::array<float,3> cross{axis[1]*v[2]-axis[2]*v[1],
        axis[2]*v[0]-axis[0]*v[2],axis[0]*v[1]-axis[1]*v[0]};
    for(int i=0;i<3;++i)v[i]=v[i]*c+cross[i]*s+axis[i]*dot*(1-c);
    return v;
}
struct LocalCameraCorrection {
    bool valid=false;
    std::array<float,16> source{}, target{};
    float yaw=0,pitch=0;
    std::uint64_t input_sequence=0, player_update=0;
};
inline LocalCameraCorrection local_camera_correction(const LocalPlayerPresentation& scene,
        const LocalPresentationState& current,std::int64_t now) {
    LocalCameraCorrection result;
    if(!scene.valid || !current.player.valid || scene.actor!=current.player.actor ||
       scene.continuity!=current.player.continuity ||
       now<current.player.sampled_ns || now-current.player.sampled_ns>250'000'000LL ||
       !xr_tracking_to_world(scene.projection,result.source))return result;
    result.target=result.source;
    result.yaw=std::remainder(current.player.yaw-scene.yaw,6.283185307179586F);
    const bool mouse=current.mouse_enabled && scene.mouse.epoch==current.mouse.epoch &&
        current.player.mouse.epoch==current.mouse.epoch;
    if(mouse && current.player.allow_yaw)result.yaw+=float(current.mouse.yaw-current.player.mouse.yaw);
    const float wanted_pitch=std::clamp(current.player.pitch+
        (mouse && current.player.allow_pitch?float(current.mouse.pitch-current.player.mouse.pitch):0.0F),
        current.player.pitch_low,current.player.pitch_high);
    result.pitch=wanted_pitch-scene.pitch;
    // Native 002349A0 builds yaw about world Y. Guest display mirrors X,
    // so pitch rotates about minus rendered right. Neither operation adds roll.
    for(int column=0;column<3;++column) {
        std::array<float,3> axis{result.target[column*4],result.target[column*4+1],result.target[column*4+2]};
        axis=rotate_axis(axis,{0,1,0},result.yaw);
        for(int i=0;i<3;++i)result.target[column*4+i]=axis[i];
    }
    const std::array<float,3> right{result.target[0],result.target[1],result.target[2]};
    for(int column=1;column<3;++column) {
        auto axis=rotate_axis({result.target[column*4],result.target[column*4+1],result.target[column*4+2]},right,-result.pitch);
        for(int i=0;i<3;++i)result.target[column*4+i]=axis[i];
    }
    for(int i=0;i<3;++i)result.target[12+i]+=current.player.origin[i]-scene.origin[i];
    result.input_sequence=current.mouse.sequence;result.player_update=current.player.update;
    result.valid=true;return result;
}
// Express the current eye in the retained scene's camera coordinates. This
// gives CPU and retained-VBO draws the same fresh body origin and orientation.
inline XrEyeFrame local_xr_eye(const XrEyeFrame& eye,const LocalCameraCorrection& c,float units) {
    if(!c.valid || !std::isfinite(units) || units<=0)return eye;
    XrEyeFrame out=eye;float m[3][3]{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)
        m[i][j]+=c.source[i*4+k]*c.target[j*4+k];
    XrShotQuat q{};const float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0) {
        const float s=2*std::sqrt(trace+1);q={(m[2][1]-m[1][2])/s,
            (m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4};
    } else {
        int i=0;if(m[1][1]>m[i][i])i=1;if(m[2][2]>m[i][i])i=2;
        const int j=(i+1)%3,k=(i+2)%3;
        const float s=2*std::sqrt((std::max)(0.0F,1+m[i][i]-m[j][j]-m[k][k]));
        if(s<0.00001F)return eye;
        q[i]=s/4;q[j]=(m[j][i]+m[i][j])/s;q[k]=(m[k][i]+m[i][k])/s;
        q[3]=(m[k][j]-m[j][k])/s;
    }
    if(!xr_normalize_shot_quat(q))return eye;
    auto orientation=xr_shot_multiply(q,{eye.orientation[0],eye.orientation[1],eye.orientation[2],eye.orientation[3]});
    if(!xr_normalize_shot_quat(orientation))return eye;
    for(int i=0;i<4;++i)out.orientation[i]=orientation[i];
    for(int i=0;i<3;++i) {
        out.position[i]=0;
        for(int j=0;j<3;++j)out.position[i]+=m[i][j]*eye.position[j]+
            c.source[i*4+j]*(c.target[12+j]-c.source[12+j])/units;
    }
    return out;
}
inline bool local_scene_projection(const ::xr64::N64RawFast3DDrawState& state,
        const LocalCameraCorrection& correction) {
    if(!correction.valid || !state.perspective_projection || state.camera_attachment)return false;
    std::array<float,16> camera{};
    if(!xr_tracking_to_world(state.source_projection,camera))return false;
    // Affect only the local camera, not other split-screen views or UI models.
    for(int i=0;i<12;++i)if(std::abs(camera[i]-correction.source[i])>0.003F)return false;
    for(int i=12;i<15;++i)if(std::abs(camera[i]-correction.source[i])>1.0F)return false;
    return true;
}
inline ::xr64::N64RawFast3DVertex local_project_vertex(::xr64::N64RawFast3DVertex v,
        const ::xr64::N64RawFast3DDrawState& state,const LocalCameraCorrection& correction) {
    if(!correction.valid)return v;
    const std::array<float,3> old_view{v.x/state.perspective_x_norm,
        v.y/state.perspective_y_norm,-v.w/state.perspective_w_norm};
    std::array<float,3> relative{};
    for(int i=0;i<3;++i) {
        relative[i]=correction.source[12+i]-correction.target[12+i];
        for(int j=0;j<3;++j)relative[i]+=old_view[j]*correction.source[j*4+i];
    }
    std::array<float,3> view{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)view[i]+=relative[j]*correction.target[i*4+j];
    std::array<float,4> source_world{correction.source[12],correction.source[13],correction.source[14],1};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)source_world[i]+=view[j]*correction.source[j*4+i];
    std::array<float,4> clip{};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)clip[i]+=source_world[j]*state.source_projection[j*4+i];
    v.x=clip[0];v.y=clip[1];v.z=clip[2];v.w=clip[3];return v;
}
} // namespace xr64::rage_wars::recomp
