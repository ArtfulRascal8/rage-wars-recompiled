#include "rage_wars_controller_mapping.hpp"

#include <cassert>
#include <cmath>

using namespace xr64::rage_wars::recomp;

int main() {
    StandardGamepadState physical;
    physical.start = true;
    physical.south = true;
    physical.east = true;
    physical.dpad_up = true;
    physical.left_shoulder = true;
    physical.right_shoulder = true;
    physical.right_trigger = kControllerDigitalAxisThreshold;
    physical.left_x = kControllerDigitalAxisThreshold;
    physical.left_y = -kControllerDigitalAxisThreshold;
    const auto pressed = map_standard_gamepad(physical);
    assert(pressed.buttons == (kN64ButtonStart | kN64ButtonA | kN64ButtonB |
            kN64ButtonDpadUp | kN64ButtonL | kN64ButtonR | kN64ButtonZ |
            kN64ButtonCRight | kN64ButtonCUp));

    physical = {};
    const auto released = map_standard_gamepad(physical);
    assert(released.buttons == 0);
    assert(released.stick_x == 0.0F && released.stick_y == 0.0F);

    physical.right_x = 3000;
    physical.right_y = -3000;
    const auto dead_zone = map_standard_gamepad(physical);
    assert(dead_zone.stick_x == 0.0F && dead_zone.stick_y == 0.0F);

    physical.right_x = 32767;
    physical.right_y = 0;
    const auto right = map_standard_gamepad(physical);
    assert(std::fabs(right.stick_x - 1.0F) < 0.0001F);
    assert(std::fabs(right.stick_y) < 0.0001F);

    physical.right_x = 0;
    physical.right_y = -32768;
    const auto up = map_standard_gamepad(physical);
    assert(std::fabs(up.stick_x) < 0.0001F);
    assert(std::fabs(up.stick_y - 1.0F) < 0.0001F);

    physical.right_y = 32767;
    const auto down = map_standard_gamepad(physical);
    assert(std::fabs(down.stick_y + 1.0F) < 0.0001F);
    physical = {};
    physical.left_x = -32768;
    physical.left_y = 32767;
    physical.right_x = 32767;
    const auto independent = map_standard_gamepad(physical);
    assert(independent.buttons == (kN64ButtonCLeft | kN64ButtonCDown));
    assert(independent.stick_x > 0.99F && independent.stick_y == 0.0F);

    KeyboardMouseState keyboard;
    keyboard.move_forward = true;
    keyboard.move_left = true;
    keyboard.jump = true;
    keyboard.action = true;
    keyboard.fire = true;
    keyboard.start = true;
    keyboard.previous_weapon = true;
    keyboard.next_weapon = true;
    keyboard.dpad_up = true;
    keyboard.mouse_x = 0.25F;
    keyboard.mouse_y = -0.5F;
    const auto keyboard_mapped = map_keyboard_mouse(keyboard);
    assert(keyboard_mapped.buttons == (kN64ButtonA | kN64ButtonB | kN64ButtonZ |
            kN64ButtonStart | kN64ButtonDpadUp | kN64ButtonL | kN64ButtonR |
            kN64ButtonCUp | kN64ButtonCLeft));
    assert(std::fabs(keyboard_mapped.stick_x - 0.25F) < 0.0001F);
    assert(std::fabs(keyboard_mapped.stick_y + 0.5F) < 0.0001F);

    keyboard = {};
    keyboard.aim_right = true;
    keyboard.aim_up = true;
    keyboard.mouse_x = 0.5F;
    keyboard.mouse_y = -0.25F;
    const auto mixed_aim = map_keyboard_mouse(keyboard);
    assert(mixed_aim.stick_x == 1.0F);
    assert(std::fabs(mixed_aim.stick_y - 0.75F) < 0.0001F);

    N64ControllerInput gamepad_input{kN64ButtonA, -0.75F, 0.25F};
    N64ControllerInput keyboard_input{kN64ButtonZ, 0.25F, 1.0F};
    const auto merged = merge_controller_input(gamepad_input, keyboard_input);
    assert(merged.buttons == (kN64ButtonA | kN64ButtonZ));
    assert(std::fabs(merged.stick_x + 0.5F) < 0.0001F);
    assert(merged.stick_y == 1.0F);
    return 0;
}
