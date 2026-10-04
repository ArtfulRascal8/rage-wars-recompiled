#pragma once
#include "rage_wars_openxr.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "rage_wars_controller_mapping.hpp"
#include "rage_wars_controls.hpp"

namespace xr64::rage_wars::recomp {

struct DesktopRendererStats {
    bool initialized = false;
    std::uint64_t task_count = 0;
    std::uint64_t presented_frames = 0;
    std::uint32_t last_task_address = 0;
    std::uint32_t last_display_list = 0;
    std::uint32_t last_triangles = 0;
};

struct DesktopControllerSnapshot {
    bool connected = false;
    std::uint16_t buttons = 0;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    std::uint64_t sequence = 0;
    std::string device_name;
};

class RageWarsDesktopRenderer {
public:
    bool initialize(std::string &error);
    bool initialize(std::string &error, bool attempt_xr);
    void request_xr_transition(XrTransitionRequest request);
    XrPresentationMode xr_presentation_mode() const;
    std::uint64_t xr_transition_revision() const;
    std::string xr_transition_status() const;
    bool pump_events();
    DesktopControllerSnapshot controller_snapshot() const;
    DesktopControllerSnapshot consume_controller_snapshot(bool gameplay, bool xr_gameplay);
    controls::LookSample consume_look_sample();
    std::uint8_t consume_weapon_cycle();
    bool submit_task(std::uint8_t *rdram, std::size_t rdram_size,
            std::uint32_t task_address, std::string &error);
    bool independent_presentation() const;
    bool present_latest(std::string& error);
    bool run_weapon_calibration(std::string& error);
    void shutdown();
    DesktopRendererStats stats() const { std::lock_guard<std::mutex> lock(stats_mutex_); return stats_; }

private:
    bool open_controller(int device_index);
    void close_controller();
    void refresh_controller_snapshot();
    void refresh_keyboard_mouse_snapshot();
    void publish_input_snapshot();
    void set_mouse_capture(bool captured);
    void set_input_focus(bool focused);

    void *window_ = nullptr;
    void *context_ = nullptr;
    void *controller_ = nullptr;
    std::int32_t controller_instance_id_ = -1;
    mutable std::mutex controller_mutex_;
    DesktopControllerSnapshot controller_snapshot_{};
    N64ControllerInput gamepad_input_{};
    N64ControllerInput menu_gamepad_input_{};
    StandardGamepadState gamepad_state_{};
    controls::GamepadMapper gamepad_mapper_;
    controls::DeviceSelector device_selector_;
    controls::Settings control_settings_{};
    controls::Device active_device_ = controls::Device::KeyboardMouse;
    controls::MouseMotion mouse_motion_;
    controls::AxisPair direct_stick_{};
    bool gameplay_context_ = false;
    bool window_focused_ = true;
    bool keyboard_activity_ = false;
    bool suppress_mouse_buttons_ = false;
    std::uint16_t last_keyboard_buttons_ = 0;
    float last_keyboard_axis_x_ = 0.0F;
    float last_keyboard_axis_y_ = 0.0F;
    float relative_mouse_x_ = 0.0F;
    float relative_mouse_y_ = 0.0F;
    float mouse_activity_distance_ = 0.0F;
    N64ControllerInput keyboard_input_{};
    N64ControllerInput menu_keyboard_input_{};
    N64ControllerInput keyboard_wheel_input_{};
    std::uint16_t menu_wheel_buttons_=0;
    bool keyboard_neutral_=true;
    bool binding_pump_blocked_=false;
    float mouse_aim_x_ = 0.0F;
    float mouse_aim_y_ = 0.0F;
    float mouse_sensitivity_ = 0.04F;
    bool mouse_captured_ = false;
    std::uint16_t latched_buttons_ = 0;
    std::uint8_t pending_weapon_cycle_=0,last_weapon_cycle_=0,last_keyboard_cycle_=0;
    std::mutex lifecycle_mutex_;
    bool shutting_down_ = false;
    mutable std::mutex stats_mutex_;
    DesktopRendererStats stats_{};
};

bool initialize_gate5_desktop(std::string& error);
bool initialize_gate5_desktop(std::string& error, bool attempt_xr);
bool pump_gate5_desktop_events();
bool gate5_independent_presentation();
bool present_gate5_desktop(std::string& error);
void request_gate5_xr_transition(XrTransitionRequest request);
XrPresentationMode gate5_xr_presentation_mode();
std::uint64_t gate5_xr_transition_revision();
std::string gate5_xr_transition_status();
bool gate5_xr_auto_ready_hint(std::string& reason);
DesktopControllerSnapshot gate5_desktop_controller_snapshot();
DesktopControllerSnapshot consume_gate5_desktop_controller_snapshot(bool gameplay = false, bool xr_gameplay = false);
XrMotionInput gate5_xr_motion_snapshot();
XrMotionInput gate5_xr_shot_snapshot();
controls::LookSample consume_gate5_desktop_look();
std::uint8_t consume_gate5_desktop_weapon_cycle();
void shutdown_gate5_desktop();
bool run_gate5_weapon_calibration(std::string& error);

} // namespace xr64::rage_wars::recomp
