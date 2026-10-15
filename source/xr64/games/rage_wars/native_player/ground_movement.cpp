#include "ground_movement.hpp"

namespace rage_wars::player {
namespace {
// Each operation rounds independently to binary32 (strict FP library build).
float add(float a, float b) { volatile float result = a + b; return result; }
float sub(float a, float b) { volatile float result = a - b; return result; }
float mul(float a, float b) { volatile float result = a * b; return result; }
float neg(float a) { volatile float result = -a; return result; }
// c.lt.s on |speed| against a maximum: unordered compares are false.
bool magnitude_exceeds(float speed, float maximum) {
    return speed < 0.0f ? maximum < neg(speed) : maximum < speed;
}
float magnitude(float value) { return value < 0.0f ? neg(value) : value; }
}

void GroundMovement::update(ObjectId actor_id, ObjectId view_id, ObjectId params_id) {
    using namespace fields::movement;
    namespace p = fields::movement_params;
    const auto actor = storage_.actor(actor_id);
    const auto view = storage_.object(view_id);
    const auto params = storage_.object(params_id);

    MotionVector motion;
    // Rotate one speed by view yaw +/- an offset. The offset is loaded once (the
    // original keeps it in $f20); yaw and speed are reloaded after each call.
    auto accumulate = [&](FloatField speed, MovementConstant which, bool subtract, bool first_term) {
        const float offset = constants_.scalar(which);
        const auto angle = [&] {
            const float yaw = view.scalar(fields::common::angle_6c);
            return subtract ? sub(yaw, offset) : add(yaw, offset);
        };
        const float first = callbacks_.first_angle_function(angle());
        const float x = mul(actor.scalar(speed), first);
        motion.x = first_term ? x : add(motion.x, x);
        if (first_term) motion.y = 0.0f;
        const float second = callbacks_.second_angle_function(angle());
        const float z = mul(actor.scalar(speed), second);
        motion.z = first_term ? z : add(motion.z, z);
    };

    if (callbacks_.game_state_blocks()) return;
    if (actor.word(freeze_word) != 0) return;

    bool impulse_triggered = false;
    bool impulse_ready = true;
    const float cooldown = actor.scalar(impulse_cooldown);
    if (!(cooldown <= 0.0f)) {
        impulse_ready = false;
        actor.scalar(impulse_cooldown, sub(cooldown, constants_.scalar(MovementConstant::frame_time)));
    }

    // Forward speed: friction above the maximum, acceleration with input, else friction.
    if (magnitude_exceeds(actor.scalar(forward_speed), params.scalar(p::forward_max))) {
        const float rate = mul(params.scalar(p::forward_friction),
                               constants_.scalar(MovementConstant::forward_over_max_friction));
        actor.scalar(forward_speed, callbacks_.friction(actor.scalar(forward_speed), rate));
    } else {
        const float input = override_ ? forward_ : actor.scalar(input_forward);
        if (input != 0.0f && actor.word(zoom_request) != 2) {
            const float accel = mul(input, params.scalar(p::forward_accel));
            const float maximum = params.scalar(p::forward_max);
            const float limit = input < 0.0f ? mul(neg(input), maximum) : mul(input, maximum);
            actor.scalar(forward_speed, callbacks_.accelerate(actor.scalar(forward_speed), accel, limit));
        } else {
            actor.scalar(forward_speed, callbacks_.friction(actor.scalar(forward_speed),
                                                           params.scalar(p::forward_friction)));
        }
    }

    accumulate(forward_speed, MovementConstant::forward_angle, false, true);

    // Strafe speed: same shape, without the zoom-request gate.
    if (magnitude_exceeds(actor.scalar(strafe_speed), params.scalar(p::strafe_max))) {
        const float rate = mul(params.scalar(p::strafe_friction),
                               constants_.scalar(MovementConstant::strafe_over_max_friction));
        actor.scalar(strafe_speed, callbacks_.friction(actor.scalar(strafe_speed), rate));
    } else {
        const float input = override_ ? strafe_ : actor.scalar(input_strafe);
        if (input != 0.0f) {
            const float accel = mul(input, params.scalar(p::strafe_accel));
            const float maximum = params.scalar(p::strafe_max);
            const float limit = input < 0.0f ? mul(neg(input), maximum) : mul(input, maximum);
            actor.scalar(strafe_speed, callbacks_.accelerate(actor.scalar(strafe_speed), accel, limit));
        } else {
            actor.scalar(strafe_speed, callbacks_.friction(actor.scalar(strafe_speed),
                                                          params.scalar(p::strafe_friction)));
        }
    }
    accumulate(strafe_speed, MovementConstant::strafe_angle, true, false);

    // Clamp by the maximum of whichever speed has the larger magnitude.
    const float strafe_magnitude = magnitude(actor.scalar(strafe_speed));
    const float forward = actor.scalar(forward_speed);
    const bool strafe_larger = forward < 0.0f ? neg(forward) < strafe_magnitude : forward < strafe_magnitude;
    callbacks_.clamp_length(motion, strafe_larger ? params.scalar(p::strafe_max) : params.scalar(p::forward_max));

    // Input-triggered forward/back impulse (buttons bits 8/4) when the cooldown allows.
    float impulse;
    const std::uint32_t buttons = impulse_ready ? actor.word(input_buttons) : 0;
    if (impulse_ready && (buttons & 8)) {
        impulse = params.scalar(p::impulse_speed);
        impulse_triggered = true;
    } else if (impulse_ready && (buttons & 4)) {
        impulse = mul(neg(params.scalar(p::impulse_speed)), constants_.scalar(MovementConstant::impulse_back_scale));
        impulse_triggered = true;
    } else {
        impulse = callbacks_.friction(actor.scalar(impulse_forward), params.scalar(p::impulse_friction));
    }
    actor.scalar(impulse_forward, impulse);
    accumulate(impulse_forward, MovementConstant::impulse_forward_angle, false, false);

    // Lateral impulse (edge bits 1/2).
    const std::uint32_t edges = impulse_ready ? actor.word(input_edges) : 0;
    if (impulse_ready && (edges & 1)) {
        impulse = mul(params.scalar(p::impulse_speed), constants_.scalar(MovementConstant::impulse_left_scale));
        impulse_triggered = true;
    } else if (impulse_ready && (edges & 2)) {
        impulse = mul(neg(params.scalar(p::impulse_speed)), constants_.scalar(MovementConstant::impulse_right_scale));
        impulse_triggered = true;
    } else {
        const float rate = mul(params.scalar(p::impulse_friction),
                               constants_.scalar(MovementConstant::impulse_lateral_friction));
        impulse = callbacks_.friction(actor.scalar(impulse_lateral), rate);
    }
    actor.scalar(impulse_lateral, impulse);
    accumulate(impulse_lateral, MovementConstant::impulse_lateral_angle, true, false);

    callbacks_.scale(motion, actor.scalar(move_scale));
    callbacks_.add_to_proposed(actor_id, motion);

    if (impulse_triggered) {
        actor.scalar(impulse_cooldown, add(actor.scalar(impulse_cooldown),
                                           constants_.scalar(MovementConstant::impulse_cooldown_add)));
        const auto sound = static_cast<std::int16_t>(params.word(p::sound_word) & 0xFFFFU);
        callbacks_.impulse_sound(sound, view.word(WordField{fields::common::x.offset}),
                                 view.word(WordField{fields::common::y.offset}),
                                 view.word(WordField{fields::common::z.offset}));
    }
}

} // namespace rage_wars::player
