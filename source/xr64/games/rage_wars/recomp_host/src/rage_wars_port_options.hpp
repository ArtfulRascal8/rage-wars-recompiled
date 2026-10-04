#pragma once

#include <cstdint>
#include "rage_wars_controls.hpp"

namespace xr64::rage_wars::port_options {

// Ordinary presentation and controls preferences. Actions, cheats, tracing,
// capture, and experimental body state deliberately do not live here.
struct Settings {
    std::uint32_t version = 1;
    float vertical_fov_degrees = 0.0F; // 0 is the guest/original projection.
    std::uint8_t window_size_preset = 0;
    bool fullscreen = false;
    bool widescreen = true;
    bool calibrated_weapon_models = true; // Preserve the existing extracted-model path.
    float master_volume = 1.0F;
    float gamma = 1.0F;
    float saturation = 1.0F;
    controls::Settings controls{};
};

constexpr float kFovGameOriginal = 0.0F;
constexpr float kFovMinimum = 40.0F;
constexpr float kFovMaximum = 110.0F;
constexpr float kGammaMinimum = 0.50F;
constexpr float kGammaMaximum = 2.00F;
constexpr float kSaturationMinimum = 0.00F;
constexpr float kSaturationMaximum = 2.00F;
constexpr std::uint8_t kWindowPresetCount = 5;

// Settings persistence belongs to UI/startup threads. Audio reads only master_gain().
// master_gain never takes the settings mutex, initializes settings, or accesses disk.
float master_gain() noexcept;
void initialize();
Settings snapshot();
void replace(Settings settings);
void reset_to_defaults();

void set_vertical_fov(float degrees);
void set_window_size_preset(std::uint8_t preset);
void set_fullscreen(bool enabled);
void set_widescreen(bool enabled);
void set_master_volume(float normalized);
void set_calibrated_weapon_models(bool enabled);
void set_gamma(float gamma);
void set_saturation(float saturation);
void set_controls(controls::Settings settings);
void set_binding(bindings::Device device, unsigned action, unsigned slot, bindings::Code code);
void select_binding(bindings::Device device, unsigned action);
void start_binding_capture(unsigned slot);
bool capture_binding_input(bindings::Device device, bindings::Code code);
void poll_binding_capture(bool neutral, bool focused);
void cancel_binding_capture();
bool binding_input_blocked();
bindings::Capture binding_editor_snapshot();
std::uint64_t ui_revision();


// A monotonic revision lets the renderer apply SDL-owned window changes once.
std::uint64_t revision();

} // namespace xr64::rage_wars::port_options
