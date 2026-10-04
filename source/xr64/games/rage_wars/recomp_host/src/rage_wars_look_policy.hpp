#pragma once
#include "rage_wars_controls.hpp"
#include <cstring>
namespace xr64::rage_wars::controls {
inline std::uint32_t guest_word(const std::uint8_t* ram, std::uint32_t offset) {
    std::uint32_t value; std::memcpy(&value, ram + offset, 4); return value;
}
inline float guest_float(const std::uint8_t* ram, std::uint32_t offset) {
    float value; std::memcpy(&value, ram + offset, 4); return value;
}
inline void guest_float_write(std::uint8_t* ram, std::uint32_t offset, float value) {
    std::memcpy(ram + offset, &value, 4);
}
inline bool local_actor(const std::uint8_t* ram, std::uint32_t actor) {
    return ram && actor >= 0x80000000U && actor <= 0x807FE918U && (actor & 3U) == 0 &&
        guest_word(ram, actor - 0x80000000U + 0x5D4) == 0;
}
inline std::uint32_t spring_value(const std::uint8_t* ram,std::uint32_t actor,
        std::uint32_t original,const Settings& settings) {
    if(!local_actor(ram,actor))return original;
    // Port 0's real controller block distinguishes the human from other actor records.
    if(guest_word(ram,actor-0x80000000U+0x698)!=0x80109328U)return original;
    return settings.look_spring ? 1U : 0U;
}
inline bool live_view(const std::uint8_t* ram, std::uint32_t actor, std::uint32_t view) {
    if (!local_actor(ram, actor) || view < 0x80000000U || view > 0x807FF000U || (view & 3U)) return false;
    const auto a = actor - 0x80000000U;
    return ram[0x140225U ^ 3U] == 1 && guest_word(ram, 0x1407D4U) == 0 &&
        guest_word(ram, a + 0x5DC) == view &&
        static_cast<std::int32_t>(guest_word(ram, a + 0x5E4)) > 0 &&
        guest_word(ram, view - 0x80000000U + 0x24) == 0;
}
struct LookDelta { float yaw = 0; float pitch = 0; };
inline LookDelta look_delta(const LookSample& sample, const Settings& settings, float game_time) {
    if (!sample.focused) return {};
    // 0x00292C90-CA8: guest time = 15 * elapsed VIs / video refresh Hz.
    // Cap stick integration at 100 ms to avoid large jumps after host stalls.
    const float seconds = finite_clamp(game_time / 15.0F, 0.0F, 0.1F, 0.0F);
    constexpr double radians = 0.017453292519943295;
    const bool touch = sample.device == Device::Touch;
    const bool pad = sample.device != Device::KeyboardMouse;
    const float sx = touch ? 1.0F : pad ? settings.controller_sensitivity_x : settings.mouse_sensitivity_x;
    const float sy = touch ? 1.0F : pad ? settings.controller_sensitivity_y : settings.mouse_sensitivity_y;
    const float ix = touch ? 1.0F : (pad ? settings.controller_invert_x : settings.mouse_invert_x) ? -1.0F : 1.0F;
    const float iy = touch ? 1.0F : (pad ? settings.controller_invert_y : settings.mouse_invert_y) ? -1.0F : 1.0F;
    double yaw = sample.stick.x * 240.0 * seconds;
    double pitch = sample.stick.y * 180.0 * seconds;
    if (!pad) {
        // Mouse displacement is an angle, never a virtual-stick speed.
        yaw += sample.mouse_x * 0.08;
        pitch -= sample.mouse_y * 0.08;
    }
    return {static_cast<float>(yaw * sx * ix * radians + (!pad ? sample.mouse_yaw : 0)),
            static_cast<float>(pitch * sy * iy * radians + (!pad ? sample.mouse_pitch : 0))};
}
// One gameplay update owns one sample; other actors never drain local input.
class LookFrame {
public:
    void begin(std::uint8_t* ram, std::uint32_t actor, const LookSample& sample, const Settings& settings) {
        spring_ = settings.look_spring;
        actor_ = actor; yaw_used_ = pitch_used_ = yaw_allowed_ = false;
        enabled_ = settings.profile == Profile::Modern && local_actor(ram, actor) &&
            guest_word(ram,actor-0x80000000U+0x698)==0x80109328U;
        delta_ = enabled_ ? look_delta(sample, settings, guest_float(ram, 0xCD648)) : LookDelta{};
    }
    bool apply(std::uint8_t* ram, std::uint32_t actor, std::uint32_t view,
            std::uint32_t config, bool pitch) {
        // Native 00220EB0 passes the actor again as the orientation owner.
        // actor+5DC is the camera output, not this routine's second argument.
        if ((pitch && spring_) || !enabled_ || actor != actor_ || view != actor ||
                !live_view(ram, actor, guest_word(ram,actor-0x80000000U+0x5DC)) ||
                config < 0x80000000U || config > 0x807FFFE8U || (config & 3U)) return false;
        const auto a = actor - 0x80000000U, v = view - 0x80000000U, c = config - 0x80000000U;
        bool& used = pitch ? pitch_used_ : yaw_used_;
        if (used) return true;
        if (pitch) {
            const float low = guest_float(ram, c + 0x10), high = guest_float(ram, c + 0x14);
            const float current = guest_float(ram, a + 0x724);
            if (!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(current) ||
                    low > high || low < -1.58F || high > 1.58F) return false;
            pitch_low_=low;pitch_high_=high;
            guest_float_write(ram, a + 0x724, std::clamp(current + delta_.pitch, low, high));
        } else {
            const float scale = guest_float(ram, c), current = guest_float(ram, v + 0x6C);
            if (!std::isfinite(scale) || !std::isfinite(current)) return false;
            yaw_allowed_=scale!=0.0F;
            if (yaw_allowed_) {
                constexpr float tau = 6.283185307179586F;
                float next = std::fmod(current + delta_.yaw, tau);
                if (next < 0) next += tau;
                guest_float_write(ram, v + 0x6C, next);
            }
        }
        used = true;
        return true;
    }
    bool yaw_applied() const { return yaw_used_ && yaw_allowed_; }
    bool pitch_applied() const { return pitch_used_; }
    float pitch_low() const { return pitch_low_; }
    float pitch_high() const { return pitch_high_; }
private:
    float pitch_low_=-1.570796F,pitch_high_=1.570796F;
    bool yaw_allowed_=false;
    std::uint32_t actor_ = 0;
    bool spring_ = false;
    bool enabled_ = false, yaw_used_ = false, pitch_used_ = false;
    LookDelta delta_{};
};
} // namespace xr64::rage_wars::controls
