#pragma once
#include <functional>
#include <string>
namespace xr64::rage_wars::recomp {
enum class XrStartupPreference { Auto, On, Off };
enum class XrPresentationMode { Desktop, StartingXR, XRRunning, StoppingXR };
enum class XrTransitionRequest { EnterVR, ReturnToPC };

// Raw OpenXR XrTime from the runtime's clock domain. Its epoch is intentionally
// not mapped to the host monotonic clock.
struct XrPredictedDisplayTime {
    long long value = 0;
};

struct XrFrameTiming {
    bool began=false, rendered=false;
    double gpu_span_ms=-1;
    // Compatibility field. It carries the raw XrTime stored in the typed field at the end of this struct.
    long long predicted_ns=0;
    float pose_y=0, pose_qy=0, pose_qw=1;
    unsigned long long gpu_sample_frame=0;
    double period_ms=0, app_gap_ms=0, wait_ms=0, acquire_ms=0, image_wait_ms=0;
    double eye_ms[2]={}, mirror_copy_ms=0, mirror_present_ms=0, end_ms=0;
    unsigned long long predicted_misses=0;
    XrPredictedDisplayTime predicted_display_time{};
};
struct XrEyeFrame {
    int width=0, height=0;
    float orientation[4]={0,0,0,1};
    float position[3]={0,0,0};
    float fov[4]={}; // left, right, up, down radians
};
struct XrMotionHand {
    bool pose_valid=false;
    float position[3]={};
    float orientation[4]={0,0,0,1};
    bool aim_pose_valid=false;
    float aim_position[3]={};
    float aim_orientation[4]={0,0,0,1};
    float stick[2]={};
    float trigger=0, grip=0;
    bool primary=false, secondary=false;
};
enum class XrPoseTimePolicy { Unavailable, DisplayPrediction, CurrentSample };
struct XrMotionInput {
    bool available=false, focused=false;
    bool menu_button=false;
    unsigned long long sequence=0;
    // Compatibility field. It carries the raw XrTime stored in the typed field at the end of this struct.
    long long predicted_ns=0;
    long long host_monotonic_ns=0;
    bool head_pose_valid=false;
    float head_position[3]={};
    float head_orientation[4]={0,0,0,1};
    XrMotionHand left, right;
    XrPredictedDisplayTime predicted_display_time{};
    XrPoseTimePolicy pose_time_policy=XrPoseTimePolicy::Unavailable;
    long long pose_target_time=0; // Runtime XrTime; never subtract a host timestamp.
    unsigned long long space_epoch=0;
};
// At a guest firing boundary, capture the most recent current-time sample.
// A bounded age is permitted; a future display-predicted pose is never a shot.
inline bool xr_shot_pose_usable(const XrMotionInput& input,long long now) {
    return input.available && input.focused && input.head_pose_valid &&
        input.right.pose_valid && input.right.aim_pose_valid &&
        input.pose_time_policy==XrPoseTimePolicy::CurrentSample && input.pose_target_time>0 &&
        input.space_epoch>0 && input.host_monotonic_ns>0 && now>=input.host_monotonic_ns &&
        now-input.host_monotonic_ns<=50'000'000LL;
}
class RageWarsOpenXr {
public:
    bool initialize(std::string& error);
    bool render(const std::function<bool(const XrEyeFrame&)>& draw, const std::function<bool()>& present_mirror, std::string& error, bool have_frame=true, XrFrameTiming* timing=nullptr);
    void shutdown();
    void request_recenter();
    bool touch_actions_ready() const;
    bool session_running() const;
    bool session_focused() const;
    XrMotionInput input_snapshot() const;
    XrMotionInput shot_snapshot() const;
};
}
