#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xr64::rage_wars::recomp {

inline constexpr std::uint16_t kN64ButtonA = 0x8000U;
inline constexpr std::uint16_t kN64ButtonB = 0x4000U;
inline constexpr std::uint16_t kN64ButtonZ = 0x2000U;
inline constexpr std::uint16_t kN64ButtonStart = 0x1000U;
inline constexpr std::uint16_t kN64ButtonDpadUp = 0x0800U;
inline constexpr std::uint16_t kN64ButtonDpadDown = 0x0400U;
inline constexpr std::uint16_t kN64ButtonDpadLeft = 0x0200U;
inline constexpr std::uint16_t kN64ButtonDpadRight = 0x0100U;
inline constexpr std::uint16_t kN64ButtonL = 0x0020U;
inline constexpr std::uint16_t kN64ButtonR = 0x0010U;
inline constexpr std::uint16_t kN64ButtonCUp = 0x0008U;
inline constexpr std::uint16_t kN64ButtonCDown = 0x0004U;
inline constexpr std::uint16_t kN64ButtonCLeft = 0x0002U;
inline constexpr std::uint16_t kN64ButtonCRight = 0x0001U;

inline constexpr float kControllerStickDeadZone = 0.18F;
inline constexpr std::int16_t kControllerDigitalAxisThreshold = 16384;

struct StandardGamepadState {
    std::uint32_t raw_buttons = 0;
    std::int16_t left_trigger = 0;
    bool start = false;
    bool south = false;
    bool east = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
    bool left_shoulder = false;
    bool right_shoulder = false;
    std::int16_t left_x = 0;
    std::int16_t left_y = 0;
    std::int16_t right_x = 0;
    std::int16_t right_y = 0;
    std::int16_t right_trigger = 0;
};

struct N64ControllerInput {
    std::uint16_t buttons = 0;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    // Host actions that have no single native N64 button: previous/next weapon.
    std::uint8_t weapon_cycle = 0;
};

struct KeyboardMouseState {
    bool confirm = false;
    bool move_forward = false;
    bool move_back = false;
    bool move_left = false;
    bool move_right = false;
    bool aim_up = false;
    bool aim_down = false;
    bool aim_left = false;
    bool aim_right = false;
    bool jump = false;
    bool action = false;
    bool fire = false;
    bool start = false;
    bool previous_weapon = false;
    bool next_weapon = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
    float mouse_x = 0.0F;
    float mouse_y = 0.0F;
};

inline float normalize_sdl_axis(std::int16_t value) {
    const float divisor = value < 0 ? 32768.0F : 32767.0F;
    return std::clamp(static_cast<float>(value) / divisor, -1.0F, 1.0F);
}

inline N64ControllerInput map_standard_gamepad(const StandardGamepadState& state) {
    N64ControllerInput result;
    if (state.south) result.buttons |= kN64ButtonA;
    if (state.east) result.buttons |= kN64ButtonB;
    if (state.start) result.buttons |= kN64ButtonStart;
    if (state.dpad_up) result.buttons |= kN64ButtonDpadUp;
    if (state.dpad_down) result.buttons |= kN64ButtonDpadDown;
    if (state.dpad_left) result.buttons |= kN64ButtonDpadLeft;
    if (state.dpad_right) result.buttons |= kN64ButtonDpadRight;
    if (state.left_shoulder) result.buttons |= kN64ButtonL;
    if (state.right_shoulder) result.buttons |= kN64ButtonR;
    if (state.right_trigger >= kControllerDigitalAxisThreshold) result.buttons |= kN64ButtonZ;
    // Modern layout: left stick drives the four movement C-buttons; right stick aims.
    // Movement retains the original digital speed; aim retains radial analog deadzone.
    if (state.left_x <= -kControllerDigitalAxisThreshold) result.buttons |= kN64ButtonCLeft;
    else if (state.left_x >= kControllerDigitalAxisThreshold) result.buttons |= kN64ButtonCRight;
    if (state.left_y <= -kControllerDigitalAxisThreshold) result.buttons |= kN64ButtonCUp;
    else if (state.left_y >= kControllerDigitalAxisThreshold) result.buttons |= kN64ButtonCDown;

    const float raw_x = normalize_sdl_axis(state.right_x);
    const float raw_y = -normalize_sdl_axis(state.right_y);
    const float magnitude = std::hypot(raw_x, raw_y);
    if (magnitude > kControllerStickDeadZone) {
        const float clamped_magnitude = (std::min)(magnitude, 1.0F);
        const float scaled_magnitude = (clamped_magnitude - kControllerStickDeadZone) /
                (1.0F - kControllerStickDeadZone);
        const float scale = scaled_magnitude / magnitude;
        result.stick_x = std::clamp(raw_x * scale, -1.0F, 1.0F);
        result.stick_y = std::clamp(raw_y * scale, -1.0F, 1.0F);
    }
    return result;
}

// Menu directions are digital or stick navigation, never mouse displacement.
inline N64ControllerInput map_gamepad_menu(const StandardGamepadState& state) {
    N64ControllerInput out;
    if (state.south) out.buttons |= kN64ButtonA;
    if (state.east) out.buttons |= kN64ButtonB;
    if (state.start) out.buttons |= kN64ButtonStart;
    if (state.left_shoulder) out.buttons |= kN64ButtonL;
    if (state.right_shoulder) out.buttons |= kN64ButtonR;
    const auto axis = [](std::int16_t v) {
        const float x = normalize_sdl_axis(v);
        return std::fabs(x) >= kControllerStickDeadZone ? x : 0.0F;
    };
    out.stick_x = axis(state.left_x);
    out.stick_y = -axis(state.left_y);
    if (out.stick_x == 0) out.stick_x = axis(state.right_x);
    if (out.stick_y == 0) out.stick_y = -axis(state.right_y);
    if (state.dpad_left || state.dpad_right)
        out.stick_x = float(state.dpad_right) - float(state.dpad_left);
    if (state.dpad_up || state.dpad_down)
        out.stick_y = float(state.dpad_up) - float(state.dpad_down);
    return out;
}
inline N64ControllerInput map_keyboard_menu(const KeyboardMouseState& state) {
    N64ControllerInput out;
    if (state.confirm || state.jump || state.fire) out.buttons |= kN64ButtonA;
    if (state.action) out.buttons |= kN64ButtonB;
    if (state.start) out.buttons |= kN64ButtonStart;
    out.stick_x = float(state.move_right || state.aim_right || state.dpad_right) -
                  float(state.move_left || state.aim_left || state.dpad_left);
    out.stick_y = float(state.move_forward || state.aim_up || state.dpad_up) -
                  float(state.move_back || state.aim_down || state.dpad_down);
    return out;
}

inline N64ControllerInput map_keyboard_mouse(const KeyboardMouseState& state) {
    N64ControllerInput result;
    if (state.jump) result.buttons |= kN64ButtonA;
    if (state.action) result.buttons |= kN64ButtonB;
    if (state.fire) result.buttons |= kN64ButtonZ;
    if (state.start) result.buttons |= kN64ButtonStart;
    if (state.dpad_up) result.buttons |= kN64ButtonDpadUp;
    if (state.dpad_down) result.buttons |= kN64ButtonDpadDown;
    if (state.dpad_left) result.buttons |= kN64ButtonDpadLeft;
    if (state.dpad_right) result.buttons |= kN64ButtonDpadRight;
    if (state.previous_weapon) result.buttons |= kN64ButtonL;
    if (state.next_weapon) result.buttons |= kN64ButtonR;
    if (state.move_forward) result.buttons |= kN64ButtonCUp;
    if (state.move_back) result.buttons |= kN64ButtonCDown;
    if (state.move_left) result.buttons |= kN64ButtonCLeft;
    if (state.move_right) result.buttons |= kN64ButtonCRight;

    const float digital_x = static_cast<float>(state.aim_right) -
            static_cast<float>(state.aim_left);
    const float digital_y = static_cast<float>(state.aim_up) -
            static_cast<float>(state.aim_down);
    result.stick_x = std::clamp(state.mouse_x + digital_x, -1.0F, 1.0F);
    result.stick_y = std::clamp(state.mouse_y + digital_y, -1.0F, 1.0F);
    return result;
}

inline N64ControllerInput merge_controller_input(
        const N64ControllerInput& gamepad, const N64ControllerInput& keyboard_mouse) {
    N64ControllerInput result;
    result.buttons = gamepad.buttons | keyboard_mouse.buttons;
    result.weapon_cycle = gamepad.weapon_cycle | keyboard_mouse.weapon_cycle;
    result.stick_x = std::clamp(gamepad.stick_x + keyboard_mouse.stick_x, -1.0F, 1.0F);
    result.stick_y = std::clamp(gamepad.stick_y + keyboard_mouse.stick_y, -1.0F, 1.0F);
    return result;
}

} // namespace xr64::rage_wars::recomp
