#include "rage_wars_local_presentation.hpp"
#include "rage_wars_port_options.hpp"
#include "rage_wars_desktop_renderer.hpp"
#include "rage_wars_xr_shot.hpp"
#include "rage_wars_xr_guest_input.hpp"
#include "rage_wars_preview_frame.hpp"
#include "rage_wars_weapon_calibration.hpp"
#include <mutex>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
namespace {
xr64::rage_wars::controls::LookFrame frame;
xr64::rage_wars::controls::LookSample look_sample;
std::mutex xr_actor_mutex;
std::uint32_t xr_actor=0;
std::array<xr64::rage_wars::recomp::PreviewWeaponFrame,2> preview_frames;
thread_local std::uint32_t xr_shot_owner=0, xr_shot_action=0, xr_shot_weapon=0;
thread_local std::uint64_t xr_shot_sequence=0;
std::int64_t xr_actor_time=0;
bool xr_barrel_tip_world(std::uint16_t weapon,
        const std::array<float,16>& camera,const xr64::rage_wars::recomp::XrMotionInput& motion,
        float units,std::array<float,3>& world) {
 using namespace xr64::rage_wars;
 namespace recomp=xr64::rage_wars::recomp;
 const auto directory=recomp::weapon_asset_directory();
 if(directory.empty()||weapon<1||weapon>14||!std::isfinite(units)||units<=0)return false;
 static std::map<unsigned,std::array<float,4>> placements;
 auto placement=placements.find(weapon);
 if(placement==placements.end()) {
  std::array<float,4> value{};
  std::ifstream file(directory/(std::to_string(weapon)+".placement"));
  if(!(file>>value[0]>>value[1]>>value[2]>>value[3]))return false;
  for(float v:value)if(!std::isfinite(v))return false;
  if(value[0]<=0||value[0]>0.01F||std::abs(value[1])>100000||std::abs(value[2])>100000||std::abs(value[3])>100000)return false;
  placement=placements.emplace(weapon,value).first;
 }
 static std::map<unsigned,recomp::WeaponCalibration> calibrations;
 auto calibration=calibrations.find(weapon);
 if(calibration==calibrations.end())calibration=calibrations.emplace(weapon,recomp::load_weapon_calibration(weapon)).first;
 std::array<float,3> muzzle_mesh{};
 std::ifstream muzzle_file(directory/(std::to_string(weapon)+".muzzle"));
 if(!(muzzle_file>>muzzle_mesh[0]>>muzzle_mesh[1]>>muzzle_mesh[2]))return false;
 muzzle_file>>std::ws;
 if(!muzzle_file.eof())return false;
 for(float value:muzzle_mesh)if(!std::isfinite(value)||std::abs(value)>100000)return false;
 recomp::XrShotQuat q{motion.right.aim_orientation[0],motion.right.aim_orientation[1],motion.right.aim_orientation[2],motion.right.aim_orientation[3]};
 if(!recomp::xr_normalize_shot_quat(q))return false;
 std::array<float,3> grip{};
 for(int i=0;i<3;++i){grip[i]=motion.right.position[i];if(!std::isfinite(grip[i])||std::abs(grip[i])>10)return false;}
 const auto tip=recomp::weapon_mesh_to_tracking(muzzle_mesh,placement->second,calibration->second,q,grip);
 for(int i=0;i<3;++i) {
  world[i]=camera[12+i];
  for(int j=0;j<3;++j)world[i]+=tip[j]*units*camera[j*4+i];
  if(!std::isfinite(world[i]))return false;
 }
 return true;
}
std::int64_t xr_now() {
 return std::chrono::duration_cast<std::chrono::nanoseconds>(
     std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
extern "C" void xr64_xr_observe_player(std::uint8_t* ram,std::uint32_t actor) {
 using namespace xr64::rage_wars;
 if(!controls::local_actor(ram,actor) ||
     controls::guest_word(ram,actor-0x80000000U+0x698)!=0x80109328U) return;
 std::lock_guard lock(xr_actor_mutex);xr_actor=actor;xr_actor_time=xr_now();
}
namespace xr64::rage_wars::recomp {
LocalPresentationMailbox& local_presentation() { static LocalPresentationMailbox state;return state; }
LocalPlayerPresentation read_local_player(const std::uint8_t* ram,std::uint32_t actor) {
 LocalPlayerPresentation player;
 if(!controls::local_actor(ram,actor) || controls::guest_word(ram,actor-0x80000000U+0x698)!=0x80109328U)return player;
 const auto a=actor-0x80000000U,view=controls::guest_word(ram,a+0x5DC);
 if(!controls::live_view(ram,actor,view))return player;
 player.valid=true;player.actor=actor;player.sampled_ns=xr_now();
 for(int i=0;i<3;++i) {player.origin[i]=controls::guest_float(ram,a+8+i*4);if(!std::isfinite(player.origin[i]))player.valid=false;}
 player.yaw=controls::guest_float(ram,a+0x6C);
 player.pitch_low=frame.pitch_low();player.pitch_high=frame.pitch_high();
 player.allow_yaw=frame.yaw_applied();player.allow_pitch=frame.pitch_applied();
 if(controls::guest_word(ram,a+0x11B4)!=0 || controls::guest_float(ram,a+0x11D8)>0)player.valid=false;
 player.pitch=controls::guest_float(ram,a+0x724);
 if(!std::isfinite(player.yaw)||!std::isfinite(player.pitch))player.valid=false;
 std::memcpy(player.projection.data(),ram+view-0x80000000U+0x1E0,64);
 return player;
}
PreviewWeaponFrame gate5_preview_weapon_frame(std::uint32_t task_address,std::uint32_t task_size) {
 std::lock_guard lock(xr_actor_mutex);
 return select_preview_weapon_frame(preview_frames,task_address,task_size,xr_now());
}
bool gate5_xr_guest_gameplay(const std::uint8_t* ram,bool base_gameplay) {
 std::lock_guard lock(xr_actor_mutex);
 const auto age=xr_now()-xr_actor_time;
 auto state=xr_guest_context(ram,age>=0 && age<=250'000'000LL?xr_actor:0,base_gameplay);
 if(!base_gameplay) {xr_actor=0;xr_actor_time=0;}
 static int reported=-1;
 if(reported!=int(state)) {
  reported=int(state);
  std::fprintf(stderr,"RW112_XR_GUEST_CONTEXT state=%d actor=%08X source=guest_team_update\n",reported,xr_actor);
 }
 return state==XrGuestContext::Playing;
}
}
extern "C" void xr64_controls_begin_player(std::uint8_t* ram, std::uint32_t actor) {
    using namespace xr64::rage_wars;
    if (!controls::local_actor(ram, actor) || controls::guest_word(ram,actor-0x80000000U+0x698)!=0x80109328U) return;
    if(controls::guest_word(ram,actor-0x80000000U+0x698)==0x80109328U &&
       (static_cast<std::int32_t>(controls::guest_word(ram,actor-0x80000000U+0x5E4))<=0 ||
        controls::guest_word(ram,0x1407D4)!=0))
        (void)recomp::consume_gate5_desktop_weapon_cycle();
    recomp::local_presentation().update_begin(xr_now());
    look_sample=recomp::consume_gate5_desktop_look();
    frame.begin(ram, actor, look_sample, port_options::snapshot().controls);
}
extern "C" int xr64_controls_apply_yaw(std::uint8_t* ram, std::uint32_t actor,
        std::uint32_t view, std::uint32_t config) {
    using namespace xr64::rage_wars;
    const bool probe=(recomp::control_sample_enabled() || recomp::local_state_sample_enabled()) && controls::local_actor(ram,actor);
    const float before=probe?controls::guest_float(ram,actor-0x80000000U+0x6C):0;
    const bool already=frame.yaw_applied();
    const bool applied = frame.apply(ram, actor, view, config, false);
    if(probe && applied && frame.yaw_applied()) {
        const auto delta=controls::look_delta(look_sample,port_options::snapshot().controls,controls::guest_float(ram,0xCD648));
        const float after=controls::guest_float(ram,actor-0x80000000U+0x6C);
        recomp::local_presentation().check_look(std::abs(std::remainder(after-before-(already?0:delta.yaw),6.28318530718F))<0.0001F);
    }
    if(applied)xr64::rage_wars::recomp::local_presentation().applied_mouse(look_sample,false);
    static bool reported = false;
    if (applied && !reported) {
        reported = true;
        std::fprintf(stderr, "RW102_MODERN_LOOK active=1 actor=%08X view=%08X config=%08X timestep=%g\n",
                actor, view, config, xr64::rage_wars::controls::guest_float(ram, 0xCD648));
    }
    return applied ? 1 : 0;
}
extern "C" int xr64_controls_apply_pitch(std::uint8_t* ram, std::uint32_t actor,
        std::uint32_t view, std::uint32_t config) {
    using namespace xr64::rage_wars;
    const bool probe=(recomp::control_sample_enabled() || recomp::local_state_sample_enabled()) && controls::local_actor(ram,actor);
    const float before=probe?controls::guest_float(ram,actor-0x80000000U+0x724):0;
    const bool already=frame.pitch_applied();
    const bool applied=frame.apply(ram, actor, view, config, true);
    if(probe && applied) {
        const auto delta=controls::look_delta(look_sample,port_options::snapshot().controls,controls::guest_float(ram,0xCD648));
        const float after=controls::guest_float(ram,actor-0x80000000U+0x724);
        const float expected=std::clamp(before+(already?0:delta.pitch),frame.pitch_low(),frame.pitch_high());
        recomp::local_presentation().check_look(std::abs(after-expected)<0.0001F);
    }
    if(applied)xr64::rage_wars::recomp::local_presentation().applied_mouse(look_sample,true);
    return applied ? 1 : 0;
}

extern "C" void xr64_controls_publish_player(std::uint8_t* ram,std::uint32_t actor) {
 using namespace xr64::rage_wars;
 if(!controls::local_actor(ram,actor) || controls::guest_word(ram,actor-0x80000000U+0x698)!=0x80109328U)return;
 // Rejected/locked axes consume their interval without retaining it for a
 // later recovery. Newer host input remains pending at its own endpoint.
 if(!frame.yaw_applied())recomp::local_presentation().applied_mouse(look_sample,false);
 if(!frame.pitch_applied())recomp::local_presentation().applied_mouse(look_sample,true);
 recomp::local_presentation().publish(recomp::read_local_player(ram,actor));
}
extern "C" std::uint32_t xr64_controls_spring_value(std::uint8_t* ram,std::uint32_t actor,std::uint32_t original) {
    using namespace xr64::rage_wars;
    const auto settings=port_options::snapshot().controls;
    const auto effective=controls::spring_value(ram,actor,original,settings);
    static int reported=-1;
    if(controls::local_actor(ram,actor) &&
            controls::guest_word(ram,actor-0x80000000U+0x698)==0x80109328U &&
            reported!=int(effective)) {
        reported=int(effective);
        std::fprintf(stderr,"RW103_LOOK_SPRING actor=%08X original=%u effective=%u\n",actor,original,effective);
    }
    return effective;
}

extern "C" std::uint32_t xr64_controls_take_weapon_cycle() {
    return xr64::rage_wars::recomp::consume_gate5_desktop_weapon_cycle();
}

// Guest 0x0021A9A4 copies actor+0x7D8..0x7E4 into a stack-local quaternion
// passed to 0x00280014. The tracked target and origin replace only that
// local weapon call; actor orientation and stereo camera remain guest-owned.
extern "C" int xr64_xr_shot_override(std::uint8_t* ram, std::uint32_t actor,
        std::uint32_t action, std::uint32_t* shot_quaternion,
        const std::uint32_t* actor_position, std::uint32_t* weapon_position,
        std::uint32_t* target_position) {
    using namespace xr64::rage_wars;
    const bool enabled=port_options::snapshot().controls.xr_motion_controls;
    if (enabled) {
        static unsigned entry_logged=0;
        if (entry_logged<16) {
            ++entry_logged;
            std::fprintf(stderr,"RW109_XR_ENTRY actor=%08X action=%u local=%d quaternion=%d\n",
                    actor,action,controls::local_actor(ram,actor)?1:0,shot_quaternion?1:0);
        }
    }
    if (!enabled || !shot_quaternion || !recomp::xr_base_gameplay(ram) ||
        !controls::local_actor(ram,actor)) return 0;
    const auto a=actor-0x80000000U;
    if (controls::guest_word(ram,a+0x698)!=0x80109328U ||
            static_cast<std::int32_t>(controls::guest_word(ram,a+0x5E4))<=0 ||
            controls::guest_word(ram,0x1407D4)!=0) return 0;
    // Captured standard weapons share this actor-owned target/muzzle call.
    // Special/transformation IDs remain native until their paths are established.
    std::uint16_t weapon=0;
    std::memcpy(&weapon,ram+((a+0x62E)^2U),sizeof(weapon));
    if (weapon<1 || weapon>14) return 0;
    const auto motion=recomp::gate5_xr_shot_snapshot();
    if (!motion.available || !motion.focused || !motion.head_pose_valid ||
            !motion.right.aim_pose_valid) return 0;
    const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto age=now-motion.host_monotonic_ns;
    if (!recomp::xr_shot_pose_usable(motion,now)) return 0;
    const auto view=controls::guest_word(ram,a+0x5DC);
    if (!weapon_position || !target_position || !recomp::xr_guest_pointer(view,0x220)) return 0;
    std::array<float,16> projection{},camera{};
    std::memcpy(projection.data(),ram+(view-0x80000000U)+0x1E0,sizeof(projection));
    if (!recomp::xr_tracking_to_world(projection,camera)) return 0;
    float units=100.0F;
    if(const char* value=std::getenv("XR64_XR_UNITS_PER_METRE")) {
        const float parsed=std::strtof(value,nullptr);
        if(std::isfinite(parsed)&&parsed>0) units=parsed;
    }
    std::array<float,3> origin{},target{};
    if (!recomp::xr_world_shot_ray(camera,motion,origin,target,units)) return 0;
    recomp::XrShotQuat original{},aimed{};
    std::memcpy(original.data(),shot_quaternion,sizeof(original));
    // The native target-to-muzzle path rebuilds direction at 0x00281088.
    // Supply the tracked origin AND target; a quaternion alone is overwritten.
    std::memcpy(weapon_position,origin.data(),sizeof(origin));
    std::memcpy(target_position,target.data(),sizeof(target));
    aimed=original;
    xr_shot_owner=actor;xr_shot_action=action;xr_shot_weapon=weapon;++xr_shot_sequence;
    static std::array<unsigned,15> logged{};
    if (logged[weapon]<16) {
        ++logged[weapon];
        std::array<float,3> actor_xyz{}, weapon_xyz{};
        if (actor_position) std::memcpy(actor_xyz.data(),actor_position,sizeof(actor_xyz));
        if (weapon_position) std::memcpy(weapon_xyz.data(),weapon_position,sizeof(weapon_xyz));
        std::fprintf(stderr,"RW109_XR_SHOT candidate=4 target=rendered_world_ray shot=%llu actor=%08X weapon=%u action=%u age_ms=%.1f base=(%.4f,%.4f,%.4f,%.4f) hand=(%.4f,%.4f,%.4f,%.4f) output=(%.4f,%.4f,%.4f,%.4f) actor_pos=(%.4f,%.4f,%.4f) weapon_pos=(%.4f,%.4f,%.4f) hand_pos=(%.4f,%.4f,%.4f) target_pos=(%.3f,%.3f,%.3f)\n",
                static_cast<unsigned long long>(xr_shot_sequence),actor,weapon,action,double(age)/1'000'000.0,
                original[0],original[1],original[2],original[3],
                motion.right.aim_orientation[0],motion.right.aim_orientation[1],
                motion.right.aim_orientation[2],motion.right.aim_orientation[3],
                aimed[0],aimed[1],aimed[2],aimed[3],
                actor_xyz[0],actor_xyz[1],actor_xyz[2],
                weapon_xyz[0],weapon_xyz[1],weapon_xyz[2],
                motion.right.aim_position[0],motion.right.aim_position[1],motion.right.aim_position[2],
                target[0],target[1],target[2]);
    }
    return 1;
}

// The native attached-particle resolver owns both creation and later updates.
// Replace only its world-position source; native code still stores object+8 and
// builds the fixed-point draw matrices. Renderer matrix retargeting is absent.
extern "C" int xr64_xr_muzzle_attachment(std::uint8_t* ram,
        std::uint32_t object,std::uint32_t output) {
    using namespace xr64::rage_wars;
    namespace recomp=xr64::rage_wars::recomp;
    if(!ram || !recomp::xr_guest_pointer(object,0x1F8) ||
       !recomp::xr_guest_pointer(output,12))return 0;
    if(!port_options::snapshot().controls.xr_motion_controls)return 0;
    const auto o=object-0x80000000U;
    if(ram[o^3U]!=2 || !(controls::guest_word(ram,o+0x5C)&0x00400000U))return 0;
    const auto script=controls::guest_word(ram,o+0x118);
    if(!recomp::xr_guest_pointer(script,0x3C) ||
       controls::guest_word(ram,script-0x80000000U)!=0x0000C000U)return 0;
    const auto parent=controls::guest_word(ram,o+0x12C);
    if(!recomp::xr_guest_pointer(parent,0x1DC))return 0;
    const auto p=parent-0x80000000U;
    if(ram[p^3U]!=1 || !(controls::guest_word(ram,p+0x100)&0x00300000U))return 0;
    const auto actor=controls::guest_word(ram,p+0x1D8);
    const auto owner=controls::guest_word(ram,o+0x124);
    if((parent!=actor && parent!=actor+0x2E8U) ||
       (owner!=actor && owner!=parent) || !controls::local_actor(ram,actor) ||
       recomp::xr_guest_context(ram,actor,recomp::xr_base_gameplay(ram))!=recomp::XrGuestContext::Playing)return 0;
    const auto a=actor-0x80000000U;
    if(controls::guest_word(ram,a+0x698)!=0x80109328U ||
       static_cast<std::int32_t>(controls::guest_word(ram,a+0x5E4))<=0)return 0;
    std::uint16_t weapon=0,action=0;
    std::memcpy(&weapon,ram+((a+0x62E)^2U),2);
    std::memcpy(&action,ram+((o+4)^2U),2);
    if(weapon<1 || weapon>14)return 0;
    const auto motion=recomp::gate5_xr_motion_snapshot();
    const auto age=xr_now()-motion.host_monotonic_ns;
    if(!motion.available || !motion.focused || !motion.head_pose_valid ||
       !motion.right.pose_valid || !motion.right.aim_pose_valid ||
       motion.host_monotonic_ns<=0 || age<0 || age>250'000'000LL)return 0;
    const auto view=controls::guest_word(ram,a+0x5DC);
    if(!recomp::xr_guest_pointer(view,0x220))return 0;
    std::array<float,16> projection{},camera{};
    std::memcpy(projection.data(),ram+(view-0x80000000U)+0x1E0,64);
    if(!recomp::xr_tracking_to_world(projection,camera))return 0;
    float units=100.0F;
    if(const char* value=std::getenv("XR64_XR_UNITS_PER_METRE")) {
        const float parsed=std::strtof(value,nullptr);
        if(std::isfinite(parsed)&&parsed>0)units=parsed;
    }
    std::array<float,3> origin{};
    if(!xr_barrel_tip_world(weapon,camera,motion,units,origin))return 0;
    std::memcpy(ram+(output-0x80000000U),origin.data(),12);
    static std::array<unsigned,15> logged{};
    if(logged[weapon]++<16)std::fprintf(stderr,
        "RW129_NATIVE_MUZZLE_ATTACHMENT weapon=%u action=%u actor=%08X object=%08X script=%08X world=(%.3f,%.3f,%.3f)\n",
        weapon,action,actor,object,script,origin[0],origin[1],origin[2]);
    return 1;
}

extern "C" void xr64_xr_shot_end() { xr_shot_owner=0; }
extern "C" void xr64_xr_shot_spawn(std::uint8_t* ram,std::uint32_t object) {
    using namespace xr64::rage_wars;
    static std::array<unsigned,15> logged{};
    if (!xr_shot_owner || xr_shot_weapon<1 || xr_shot_weapon>14 ||
            logged[xr_shot_weapon]>=128 || !recomp::xr_guest_pointer(object,0x180)) return;
    ++logged[xr_shot_weapon];
    const auto o=object-0x80000000U;
    std::fprintf(stderr,"RW113_XR_SPAWN shot=%llu owner=%08X weapon=%u action=%u object=%08X type=%08X pos=(%.3f,%.3f,%.3f) velocity=(%.3f,%.3f,%.3f) direction=(%.5f,%.5f,%.5f)\n",
        static_cast<unsigned long long>(xr_shot_sequence),xr_shot_owner,xr_shot_weapon,xr_shot_action,object,
        controls::guest_word(ram,o),
        controls::guest_float(ram,o+8),controls::guest_float(ram,o+12),controls::guest_float(ram,o+16),
        controls::guest_float(ram,o+0x1C),controls::guest_float(ram,o+0x20),controls::guest_float(ram,o+0x24),
        controls::guest_float(ram,o+0x174),controls::guest_float(ram,o+0x178),controls::guest_float(ram,o+0x17C));
}


extern "C" void xr64_xr_weapon_matrix(std::uint8_t* ram,std::uint32_t model) {
    using namespace xr64::rage_wars;
    if(model<0x800002E8U) return;
    const std::uint32_t actor=model-0x2E8U;
    if(!port_options::snapshot().controls.xr_motion_controls) return;
    if(!controls::local_actor(ram,actor) ||
       recomp::xr_guest_context(ram,actor,recomp::xr_base_gameplay(ram))!=recomp::XrGuestContext::Playing) return;
    const auto a=actor-0x80000000U;
    if(controls::guest_word(ram,a+0x698)!=0x80109328U ||
       static_cast<std::int32_t>(controls::guest_word(ram,a+0x5E4))<=0 ||
       controls::guest_word(ram,0x1407D4)!=0) return;
    std::uint16_t weapon=0;
    std::memcpy(&weapon,ram+((a+0x62E)^2U),2);
    if(weapon!=4 && weapon!=6) return;
    const auto motion=recomp::gate5_xr_motion_snapshot();
    const auto age=xr_now()-motion.host_monotonic_ns;
    if(!motion.available || !motion.focused || motion.host_monotonic_ns<=0 ||
       age<0 || age>250'000'000LL) return;
    const auto view=controls::guest_word(ram,a+0x5DC);
    if(!recomp::xr_guest_pointer(view,0x220)) return;
    std::array<float,16> projection{},camera{},native{},original{},result{};
    std::memcpy(projection.data(),ram+(view-0x80000000U)+0x1E0,64);
    std::memcpy(native.data(),ram+(view-0x80000000U)+0x160,64);
    std::memcpy(original.data(),ram+(model-0x80000000U)+0x74,64);
    if(!recomp::xr_tracking_to_world(projection,camera)) return;
    float units=100;
    if(const char* value=std::getenv("XR64_XR_UNITS_PER_METRE")) {
        const float parsed=std::strtof(value,nullptr);
        if(std::isfinite(parsed)&&parsed>0) units=parsed;
    }
    if(!recomp::xr_tracked_weapon_matrix(native,camera,motion,original,result,units)) return;
    std::memcpy(ram+(model-0x80000000U)+0x74,result.data(),64);
    static unsigned logs=0;
    if(logs++<4) std::fprintf(stderr,"RW115_XR_WEAPON model=%08X actor=%08X weapon=%u pivot=(%.3f,%.3f,%.3f) grip_calibrated=0\n",
        model,actor,weapon,result[12],result[13],result[14]);
}

// Guest0021CD50: called immediately after00249E08 completes the local weapon list.
extern "C" void xr64_preview_weapon_draw_complete(std::uint8_t* ram,std::uint32_t actor) {
 using namespace xr64::rage_wars;
 if(!controls::local_actor(ram,actor) ||
    controls::guest_word(ram,actor-0x80000000U+0x698)!=0x80109328U)return;
 recomp::PreviewWeaponFrame captured;
 if(recomp::xr_guest_context(ram,actor,recomp::xr_base_gameplay(ram))==recomp::XrGuestContext::Playing) {
  const auto a=actor-0x80000000U;
  const auto list=controls::guest_word(ram,a+0x2E8+0xC0),view=controls::guest_word(ram,a+0x5DC);
  if(recomp::xr_guest_pointer(list,8)&&recomp::xr_guest_pointer(view,0x220)) {
   captured.actor=actor;captured.list=list;
   std::uint16_t weapon=0;std::memcpy(&weapon,ram+((a+0x62E)^2U),2);captured.weapon=weapon;
   std::memcpy(captured.projection.data(),ram+(view-0x80000000U)+0x1E0,64);
   captured.recorded_ns=xr_now();
   captured.player=recomp::local_presentation().capture(recomp::read_local_player(ram,actor));
  }
 }
 const auto buffer=controls::guest_word(ram,0xCD63CU);
 static unsigned recorded_logs=0;
 if(captured.actor && recorded_logs++<12)std::fprintf(stderr,"RW118_PREVIEW_RECORD buffer=%u actor=%08X weapon=%u list=%08X\n",buffer,captured.actor,captured.weapon,captured.list);
 std::lock_guard lock(xr_actor_mutex);
 if(buffer<preview_frames.size())preview_frames[buffer]=captured;
 else preview_frames={};
}
