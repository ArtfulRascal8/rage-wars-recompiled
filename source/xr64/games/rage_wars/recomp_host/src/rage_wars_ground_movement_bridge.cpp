#include "rage_wars_ground_movement_bridge.h"
#include "rage_wars_look_policy.hpp"
#include "rage_wars_feature_defaults.hpp"
#include "recomp_memory_shim.h"
#include "funcs.h"
#include "ground_movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {
using namespace rage_wars::player;
gpr argument(std::uint32_t word) { return static_cast<gpr>(static_cast<std::int32_t>(word)); }

// Same contract as the PlayerView adapter: translate through the active
// compatibility resolver per access and never cache backing pointers.
class CompatibilityWords final : public WordStorage {
public:
    explicit CompatibilityWords(std::uint8_t* memory) : memory_(memory) {}
    std::uint32_t load_word(ObjectId object, std::uint32_t offset) const override {
        std::uint32_t value;
        std::memcpy(&value, resolve(object, offset), sizeof(value));
        return value;
    }
    void store_word(ObjectId object, std::uint32_t offset, std::uint32_t value) override {
        std::memcpy(resolve(object, offset), &value, sizeof(value));
    }
private:
    std::uint8_t* resolve(ObjectId object, std::uint32_t offset) const {
        const std::uint32_t address32 = object + offset;
        if ((address32 & 3U) != 0) throw std::runtime_error("Unaligned GroundMovement word field");
        auto* first = xr64_recomp_resolve_address(memory_, argument(address32));
        auto* last = xr64_recomp_resolve_address(memory_, argument(address32 + 3U));
        if (last != first + 3) throw std::runtime_error("Noncontiguous GroundMovement word field");
        return first;
    }
    std::uint8_t* memory_;
};

class CompatibilityConstants final : public MovementConstants {
public:
    explicit CompatibilityConstants(const CompatibilityWords& words) : words_(words) {}
    float scalar(MovementConstant value) const override {
        static constexpr std::array<ObjectId, 12> addresses{
            0x800C27E4, 0x800C27E8, 0x800C27EC, 0x800C27F0, 0x800C27F4, 0x800C27F8,
            0x800C27FC, 0x800C2800, 0x800C2804, 0x800C2808, 0x800C280C, 0x800CD648};
        return std::bit_cast<float>(words_.load_word(addresses.at(static_cast<std::size_t>(value)), 0));
    }
private:
    const CompatibilityWords& words_;
};

// Every dependency runs through its qualified guest ABI inside the original
// 0x48-byte frame. The motion vector is staged at frame+0x18, as the original
// keeps it, for the helpers that take its address.
class CompatibilityCallbacks final : public MovementCallbacks {
public:
    CompatibilityCallbacks(std::uint8_t* memory, recomp_context& context, CompatibilityWords& words)
        : memory_(memory), context_(context), words_(words),
          frame_(static_cast<std::uint32_t>(ADD32(context.r29, -0x48))) {}
    bool game_state_blocks() override {
        enter();
        resident_wave9_func_00245764(memory_, &context_);
        return context_.r2 != 0;
    }
    float accelerate(float current, float accel, float limit) override {
        enter();
        context_.f12.fl = current;
        context_.f14.fl = accel;
        context_.r6 = argument(std::bit_cast<std::uint32_t>(limit));
        trace_func_00274690_r00075290(memory_, &context_);
        return context_.f0.fl;
    }
    float friction(float current, float rate) override {
        enter();
        context_.f12.fl = current;
        context_.f14.fl = rate;
        resident_wave14_func_00274790(memory_, &context_);
        return context_.f0.fl;
    }
    float first_angle_function(float value) override {
        enter();
        context_.f12.fl = value;
        resident_wave9_func_002B7060(memory_, &context_);
        return context_.f0.fl;
    }
    float second_angle_function(float value) override {
        enter();
        context_.f12.fl = value;
        resident_wave14_func_002B6490(memory_, &context_);
        return context_.f0.fl;
    }
    void clamp_length(MotionVector& v, float maximum) override {
        enter();
        stage(v);
        context_.r4 = argument(slot());
        context_.r5 = argument(std::bit_cast<std::uint32_t>(maximum));
        trace_func_00272738_r00073338(memory_, &context_);
        v = staged();
    }
    void scale(MotionVector& v, float factor) override {
        enter();
        stage(v);
        context_.r4 = context_.r5 = argument(slot());
        context_.r6 = argument(std::bit_cast<std::uint32_t>(factor));
        resident_wave14_func_00271F8C(memory_, &context_);
        v = staged();
    }
    void add_to_proposed(ObjectId actor, const MotionVector& v) override {
        enter();
        stage(v);
        context_.r4 = context_.r5 = argument(actor + fields::movement::proposed_position);
        context_.r6 = argument(slot());
        resident_wave14_func_00271F24(memory_, &context_);
    }
    void impulse_sound(std::int16_t sound, std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        enter();
        context_.r4 = static_cast<gpr>(static_cast<std::int64_t>(sound));
        context_.r5 = argument(x);
        context_.r6 = argument(y);
        context_.r7 = argument(z);
        words_.store_word(frame_, 0x10, 0);
        words_.store_word(frame_, 0x14, 0xFFFFFFFFU);
        trace_func_0025DDF4_r0005E9F4(memory_, &context_);
    }
private:
    void enter() { context_.r29 = argument(frame_); }
    std::uint32_t slot() const { return frame_ + 0x18; }
    void stage(const MotionVector& v) {
        words_.store_word(slot(), 0, std::bit_cast<std::uint32_t>(v.x));
        words_.store_word(slot(), 4, std::bit_cast<std::uint32_t>(v.y));
        words_.store_word(slot(), 8, std::bit_cast<std::uint32_t>(v.z));
    }
    MotionVector staged() const {
        return {std::bit_cast<float>(words_.load_word(slot(), 0)),
                std::bit_cast<float>(words_.load_word(slot(), 4)),
                std::bit_cast<float>(words_.load_word(slot(), 8))};
    }
    std::uint8_t* memory_;
    recomp_context& context_;
    CompatibilityWords& words_;
    std::uint32_t frame_;
};

struct PreserveContext {
    recomp_context& context;
    recomp_context saved;
    ~PreserveContext() { context = saved; }
};

bool enabled() {
    // On by default; XR64_NATIVE_GROUND_MOVEMENT=0 switches it off.
    static const bool choice = xr64::rage_wars::recomp::feature_default_on("XR64_NATIVE_GROUND_MOVEMENT");
    return choice;
}

// The five per-state parameter records passed by the qualified direct callers.
bool known_params(std::uint32_t params) {
    switch (params) {
    case 0x800C9400: case 0x800C9448: case 0x800C946C: case 0x800C9490: case 0x800C94FC:
        return true;
    default:
        return false;
    }
}
// VR analog locomotion. The game's own sign convention for the input axes is
// learned while digital movement and the stick agree, so no sign is guessed.
// The vector is rotated by the reference yaw (head or hand direction) after the
// signs are learned from the raw, unrotated components the digital path saw.
extern "C" int xr64_xr_locomotion_stick(float* x, float* y, float* yaw);
// Only the human on port 0 takes the headset stick. The same routine moves
// every grounded actor; applying the override to them froze AI (zero input
// while the player stood still) and drove it with the player's stick.
bool local_human(const std::uint8_t* memory, std::uint32_t actor) {
    if (actor < 0x80000000U || actor > 0x807FE918U || (actor & 3U)) return false;
    const auto word = [&](std::uint32_t offset) {
        std::uint32_t value; std::memcpy(&value, memory + (actor - 0x80000000U) + offset, 4); return value;
    };
    return word(0x5D4) == 0 && word(0x698) == ::xr64::rage_wars::controls::look_input_block();
}
void learn_sign(float stick, float field, float& sign) {
    if (field != 0.0F && std::fabs(stick) > 0.3F) sign = (field > 0.0F) == (stick > 0.0F) ? 1.0F : -1.0F;
}
} // namespace

extern "C" int xr64_ground_movement_analog(float stick_x, float stick_y, float yaw, float field_forward,
                                           float field_strafe, float* forward, float* strafe) {
    static float forward_sign = 0, strafe_sign = 0;
    learn_sign(stick_y, field_forward, forward_sign);
    learn_sign(stick_x, field_strafe, strafe_sign);
    if (forward_sign == 0.0F || strafe_sign == 0.0F) return 0; // not learned yet: original values
    const float length = std::hypot(stick_x, stick_y);
    const float magnitude = length < 0.1F ? 0.0F : std::min(1.0F, (length - 0.1F) / 0.9F);
    const float scale = length > 0.0F ? magnitude / length : 0.0F;
    const float c = std::cos(yaw), s = std::sin(yaw);
    // Stick +y forward, +x right; yaw counter-clockwise: forward' = y c + x s, right' = x c - y s.
    *forward = forward_sign * (stick_y * c + stick_x * s) * scale;
    *strafe = strafe_sign * (stick_x * c - stick_y * s) * scale;
    return 1;
}

extern "C" int xr64_ground_movement_try_update(std::uint8_t* memory, recomp_context* context) {
    if (!enabled() || context == nullptr || memory == nullptr) return 0;
    if (!context->mips3_float_mode) return 0;
    const auto actor = static_cast<ObjectId>(context->r4);
    const auto view = static_cast<ObjectId>(context->r5);
    const auto params = static_cast<ObjectId>(context->r6);
    if (actor == 0 || view == 0 || !known_params(params)) return 0;

    PreserveContext restore{*context, *context};
    CompatibilityWords words(memory);
    RageWarsActorStorage storage(words);
    CompatibilityConstants constants(words);
    CompatibilityCallbacks callbacks(memory, *context, words);
    GroundMovement movement(storage, constants, callbacks);
    float stick_x = 0, stick_y = 0, yaw = 0, forward = 0, strafe = 0;
    if (local_human(memory, actor) && xr64_xr_locomotion_stick(&stick_x, &stick_y, &yaw)) {
        const auto a = storage.actor(actor);
        if (xr64_ground_movement_analog(stick_x, stick_y, yaw, a.scalar(fields::movement::input_forward),
                                        a.scalar(fields::movement::input_strafe), &forward, &strafe))
            movement.set_input_override(forward, strafe);
    }
    movement.update(actor, view, params);
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    if (!reported.test_and_set(std::memory_order_relaxed)) {
        std::fprintf(stderr, "GROUND_MOVEMENT_NATIVE_UPDATE_COMPLETED\n");
    }
    return 1;
}
