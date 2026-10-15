#pragma once

#include "actor_storage.hpp"

namespace rage_wars::player {

// Recovered 002233CC ground movement integrator. Field roles come from
// docs/actor-struct-map-20261009; the view argument supplies yaw (+0x6C) and the
// sound position, and is the actor itself at the observed Player_Tick sites.
namespace fields::movement {
inline constexpr WordField freeze_word{0x11B8}, input_buttons{0x6B0}, input_edges{0x6B8};
inline constexpr WordField zoom_request{0x7E8};
inline constexpr FloatField impulse_cooldown{0x6DC}, forward_speed{0x6C0}, strafe_speed{0x6C4};
inline constexpr FloatField input_forward{0x6A8}, input_strafe{0x6A4};
inline constexpr FloatField impulse_forward{0x6D4}, impulse_lateral{0x6D8}, move_scale{0x66C};
inline constexpr std::uint32_t proposed_position = 0x6E8;
}
// Per-state record (0x800C9400 run/jump, 0x800C9490, 0x800C946C, 0x800C94FC, 0x800C9448).
namespace fields::movement_params {
inline constexpr FloatField forward_accel{0x00}, forward_max{0x04}, forward_friction{0x08};
inline constexpr FloatField strafe_accel{0x0C}, strafe_max{0x10}, strafe_friction{0x14};
inline constexpr FloatField impulse_speed{0x18}, impulse_friction{0x1C};
inline constexpr WordField sound_word{0x20}; // halfword +0x22 is the low half
}

// Read live at each use, as the original reloads them from memory.
enum class MovementConstant {
    forward_over_max_friction, // 800C27E4
    forward_angle,             // 800C27E8
    strafe_over_max_friction,  // 800C27EC
    strafe_angle,              // 800C27F0
    impulse_back_scale,        // 800C27F4
    impulse_forward_angle,     // 800C27F8
    impulse_left_scale,        // 800C27FC
    impulse_right_scale,       // 800C2800
    impulse_lateral_friction,  // 800C2804
    impulse_lateral_angle,     // 800C2808
    impulse_cooldown_add,      // 800C280C
    frame_time                 // 800CD648
};
class MovementConstants {
public:
    virtual ~MovementConstants() = default;
    [[nodiscard]] virtual float scalar(MovementConstant) const = 0;
};

struct MotionVector { float x{}, y{}, z{}; };

// One callback per original direct call, in original order. Callbacks may
// mutate storage; the integrator reloads every field after each call.
class MovementCallbacks {
public:
    virtual ~MovementCallbacks() = default;
    [[nodiscard]] virtual bool game_state_blocks() = 0;                  // 00245764
    [[nodiscard]] virtual float accelerate(float current, float accel, float limit) = 0; // 00274690
    [[nodiscard]] virtual float friction(float current, float rate) = 0; // 00274790
    [[nodiscard]] virtual float first_angle_function(float) = 0;       // 002B7060
    [[nodiscard]] virtual float second_angle_function(float) = 0;      // 002B6490
    virtual void clamp_length(MotionVector&, float maximum) = 0;       // 00272738
    virtual void scale(MotionVector&, float factor) = 0;               // 00271F8C(v, v, s)
    virtual void add_to_proposed(ObjectId actor, const MotionVector&) = 0; // 00271F24(a+6E8, a+6E8, v)
    virtual void impulse_sound(std::int16_t sound, std::uint32_t x, std::uint32_t y,
                               std::uint32_t z) = 0;                    // 0025DDF4(s, x, y, z, 0, -1)
};

// Original-rate behavior only; dt comes from the game's frame-time global.
class GroundMovement {
public:
    GroundMovement(RageWarsActorStorage& storage, MovementConstants& constants,
                   MovementCallbacks& callbacks)
        : storage_(storage), constants_(constants), callbacks_(callbacks) {}
    void update(ObjectId actor, ObjectId view, ObjectId params);
    // Optional analog input (VR locomotion): replaces the input axes read from
    // +0x6A8/+0x6A4 for this update only. Default off; original behaviour.
    void set_input_override(float forward, float strafe) { override_ = true; forward_ = forward; strafe_ = strafe; }
private:
    RageWarsActorStorage& storage_;
    MovementConstants& constants_;
    MovementCallbacks& callbacks_;
    bool override_ = false;
    float forward_ = 0.0F, strafe_ = 0.0F;
};

} // namespace rage_wars::player
