#pragma once

#include "rage_wars_controller_mapping.hpp"
#include "rage_wars_bindings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xr64::rage_wars::controls {

enum class Profile : std::uint8_t { Modern, Classic };
enum class DeviceMode : std::uint8_t { Auto, Controller, KeyboardMouse };
enum class Device : std::uint8_t { KeyboardMouse, Controller, Touch };
enum class ResponseCurve : std::uint8_t { Linear, Precision };

struct Settings {
    Profile profile = Profile::Modern;
    DeviceMode device_mode = DeviceMode::Auto;
    ResponseCurve response_curve = ResponseCurve::Linear;
    bool look_spring = false;
    bool swap_sticks = false;
    bindings::Set keyboard_bindings = bindings::keyboard_defaults();
    bindings::Set controller_bindings = bindings::controller_defaults();
    bindings::Set xr_bindings = bindings::xr_defaults();
    float xr_turn_speed=0.5F, xr_move_threshold=0.25F, xr_trigger_threshold=0.55F;
    bool xr_motion_controls = true;
    bool controller_invert_x = false;
    bool controller_invert_y = false;
    bool mouse_invert_x = false;
    bool mouse_invert_y = false;
    float controller_sensitivity_x = 1.0F;
    float controller_sensitivity_y = 1.0F;
    float mouse_sensitivity_x = 1.0F;
    float mouse_sensitivity_y = 1.0F;
    float aim_deadzone = 0.08F;
    float movement_threshold = 0.20F;
    bool operator==(const Settings&) const = default;
};

inline float finite_clamp(float value, float lo, float hi, float fallback) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

inline Settings validate(Settings settings) {
    if (settings.profile > Profile::Classic) settings.profile = Profile::Modern;
    if (settings.device_mode > DeviceMode::KeyboardMouse) settings.device_mode = DeviceMode::Auto;
    if (settings.response_curve > ResponseCurve::Precision) settings.response_curve = ResponseCurve::Linear;
    settings.controller_sensitivity_x = finite_clamp(settings.controller_sensitivity_x, 0.25F, 3.0F, 1.0F);
    settings.controller_sensitivity_y = finite_clamp(settings.controller_sensitivity_y, 0.25F, 3.0F, 1.0F);
    settings.mouse_sensitivity_x = finite_clamp(settings.mouse_sensitivity_x, 0.25F, 3.0F, 1.0F);
    settings.mouse_sensitivity_y = finite_clamp(settings.mouse_sensitivity_y, 0.25F, 3.0F, 1.0F);
    settings.aim_deadzone = finite_clamp(settings.aim_deadzone, 0.0F, 0.30F, 0.08F);
    settings.movement_threshold = finite_clamp(settings.movement_threshold, 0.10F, 0.60F, 0.20F);
    settings.keyboard_bindings = bindings::validate(settings.keyboard_bindings, bindings::Device::KeyboardMouse);
    settings.controller_bindings = bindings::validate(settings.controller_bindings, bindings::Device::Controller);
    settings.xr_bindings=bindings::validate(settings.xr_bindings,bindings::Device::XR);
    settings.xr_turn_speed=finite_clamp(settings.xr_turn_speed,0.1F,1.0F,0.5F);
    settings.xr_move_threshold=finite_clamp(settings.xr_move_threshold,0.15F,0.6F,0.25F);
    settings.xr_trigger_threshold=finite_clamp(settings.xr_trigger_threshold,0.2F,0.9F,0.55F);
    return settings;
}

struct AxisPair { float x = 0.0F; float y = 0.0F; };

inline AxisPair radial_aim(std::int16_t x, std::int16_t y, const Settings& settings) {
    const float raw_x = recomp::normalize_sdl_axis(x);
    const float raw_y = -recomp::normalize_sdl_axis(y); // N64 convention: up is positive.
    const float magnitude = std::hypot(raw_x, raw_y);
    if (magnitude <= settings.aim_deadzone) return {};
    float length = ((std::min)(magnitude, 1.0F) - settings.aim_deadzone) /
            (1.0F - settings.aim_deadzone);
    if (settings.response_curve == ResponseCurve::Precision) length *= length;
    return {raw_x * length / magnitude, raw_y * length / magnitude};
}

class GamepadMapper {
public:
    recomp::N64ControllerInput map(const recomp::StandardGamepadState& state,
            const Settings& settings, bool gameplay) {
        if(!gameplay) {movement_=0;return recomp::map_gamepad_menu(state);}
        auto axes=state;
        if(settings.swap_sticks) {std::swap(axes.left_x,axes.right_x);std::swap(axes.left_y,axes.right_y);}
        const auto buttons=bindings::map(settings.controller_bindings,[&](bindings::Code code){return bindings::pad_down(state,code);});
        if(settings.profile==Profile::Classic) {
            movement_=0;
            auto classic=recomp::map_standard_gamepad(axes);
            classic.buttons=(classic.buttons&0xFU)|buttons.buttons;
            classic.weapon_cycle=buttons.weapon_cycle;
            classic.stick_x=std::clamp(classic.stick_x+buttons.stick_x,-1.0F,1.0F);
            classic.stick_y=std::clamp(classic.stick_y+buttons.stick_y,-1.0F,1.0F);
            return classic;
        }
        auto result=buttons;
        const float x = recomp::normalize_sdl_axis(axes.left_x);
        const float y = recomp::normalize_sdl_axis(axes.left_y);
        const float press = settings.movement_threshold;
        const float release = press * 0.75F;
        update_direction(-x, recomp::kN64ButtonCLeft, press, release);
        update_direction(x, recomp::kN64ButtonCRight, press, release);
        update_direction(-y, recomp::kN64ButtonCUp, press, release);
        update_direction(y, recomp::kN64ButtonCDown, press, release);
        result.buttons |= movement_;
        const auto aim = radial_aim(axes.right_x, axes.right_y, settings);
        result.stick_x = std::clamp(aim.x+buttons.stick_x,-1.0F,1.0F);
        result.stick_y = std::clamp(aim.y+buttons.stick_y,-1.0F,1.0F);
        return result;
    }
    void reset() { movement_ = 0; }
private:
    void update_direction(float value, std::uint16_t bit, float press, float release) {
        if (value >= press) movement_ |= bit;
        else if (value <= release) movement_ &= static_cast<std::uint16_t>(~bit);
    }
    std::uint16_t movement_ = 0;
};

// An idle held stick does not repeatedly reclaim Auto from the mouse.
// Activity is a new button press or deliberate axis change above the noise floor.
struct DeviceActivity {
    bool available = false, focused = false;
    std::uint32_t buttons = 0;
    float axes[4]{};
};

class DeviceSelector {
public:
    Device update(DeviceMode mode, bool connected, const recomp::StandardGamepadState& pad,
            bool keyboard_activity, bool focused, const DeviceActivity& touch = {}) {
        bool gamepad_activity = false;
        if (connected && focused) {
            const auto physical_buttons=bindings::pad_buttons(pad) |
                (pad.left_trigger>=recomp::kControllerDigitalAxisThreshold ? 1U<<30 : 0) |
                (pad.right_trigger>=recomp::kControllerDigitalAxisThreshold ? 1U<<31 : 0);
            gamepad_activity = (physical_buttons & ~last_buttons_) != 0;
            const float axes[] = {recomp::normalize_sdl_axis(pad.left_x),
                    recomp::normalize_sdl_axis(pad.left_y),
                    recomp::normalize_sdl_axis(pad.right_x),
                    recomp::normalize_sdl_axis(pad.right_y)};
            for (int i = 0; i < 4; ++i) {
                if (std::fabs(axes[i]) < 0.20F) {
                    activity_axes_[i] = 0.0F;
                } else if (std::fabs(axes[i] - activity_axes_[i]) >= 0.12F) {
                    gamepad_activity = true;
                    activity_axes_[i] = axes[i];
                }
            }
            last_buttons_ = physical_buttons;
        } else {
            last_buttons_ = 0;
            for (float& value : activity_axes_) value = 0.0F;
        }
        bool touch_activity = false;
        const bool touch_ready = touch.available && touch.focused;
        if (touch_ready) {
            touch_activity = (touch.buttons & ~touch_buttons_) != 0;
            for (int i = 0; i < 4; ++i) {
                const float axis = std::isfinite(touch.axes[i]) ? touch.axes[i] : 0.0F;
                if (std::fabs(axis) < 0.20F) touch_axes_[i] = 0.0F;
                else if (std::fabs(axis - touch_axes_[i]) >= 0.12F) {
                    touch_activity = true;
                    touch_axes_[i] = axis;
                }
            }
            touch_buttons_ = touch.buttons;
        } else {
            touch_buttons_ = 0;
            for (float& axis : touch_axes_) axis = 0;
        }
        if (mode == DeviceMode::Controller && connected && focused) active_ = Device::Controller;
        else if (mode == DeviceMode::KeyboardMouse && focused) active_ = Device::KeyboardMouse;
        else if (focused && keyboard_activity) active_ = Device::KeyboardMouse;
        else if (gamepad_activity) active_ = Device::Controller;
        else if (touch_activity) active_ = Device::Touch;
        else if ((active_ == Device::Touch && !touch_ready) ||
                 (active_ == Device::Controller && (!connected || !focused)))
            active_ = focused ? Device::KeyboardMouse : touch_ready ? Device::Touch : Device::KeyboardMouse;
        return active_;
    }
    Device active() const { return active_; }
private:
    Device active_ = Device::KeyboardMouse;
    std::uint32_t last_buttons_ = 0;
    float activity_axes_[4]{};
    std::uint32_t touch_buttons_ = 0;
    float touch_axes_[4]{};
};

inline recomp::N64ControllerInput route_input(Device selected,
        const recomp::N64ControllerInput& pad, const recomp::N64ControllerInput& keyboard,
        bool gameplay, bool focused) {
    if (!focused) return {};
    // Both devices can navigate menus, including recovery from a disconnected selected pad.
    if (!gameplay) return recomp::merge_controller_input(pad, keyboard);
    auto result = selected == Device::Controller ? pad : selected == Device::KeyboardMouse ? keyboard : recomp::N64ControllerInput{};
    result.buttons |= keyboard.buttons & recomp::kN64ButtonStart;
    return result;
}

// Separate motion ownership from N64 reads. Gameplay drains this exactly once
// at the player-look boundary; status queries cannot consume it.
struct LookSample {
    Device device = Device::KeyboardMouse;
    AxisPair stick{};
    double mouse_x = 0.0;
    double mouse_y = 0.0; // SDL convention: down is positive.
    bool focused = false;
    std::uint64_t mouse_epoch=0, mouse_sequence=0;
    double mouse_yaw=0, mouse_pitch=0, mouse_yaw_total=0, mouse_pitch_total=0;
};

class MouseMotion {
public:
    void add(double x, double y) {
        if (std::isfinite(x) && std::isfinite(y)) { x_ += x; y_ += y; }
    }
    AxisPair take() {
        const AxisPair result{static_cast<float>(x_), static_cast<float>(y_)};
        clear();
        return result;
    }
    void clear() { x_ = y_ = 0.0; }
private:
    double x_ = 0.0;
    double y_ = 0.0;
};

} // namespace xr64::rage_wars::controls
