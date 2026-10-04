#include "rage_wars_port_options.hpp"
#if defined(XR64_DEMO_BUILD)
#include "rage_wars_demo_launcher.hpp"
#include "rage_wars_desktop_renderer.hpp"
#include <atomic>
#endif
#include "rage_wars_native_pc_option_ids.h"

#include <Windows.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>

#if defined(XR64_DEMO_BUILD)
namespace {
std::atomic<std::uint64_t> g_vr_menu_revision{0};
std::string g_vr_preference_save_error;
}
extern "C" void xr64_pc_vr_action();
#endif

namespace xr64::rage_wars::port_options {
namespace {

std::mutex g_mutex;
Settings g_settings{};
static_assert(std::atomic<float>::is_always_lock_free);
std::atomic<float> g_master_gain{1.0F};
std::uint64_t g_revision = 0;
bool g_initialized = false;
bindings::Capture g_binding_editor;
std::uint64_t g_editor_revision=0;

float finite_clamp(float value, float minimum, float maximum, float fallback) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

Settings validated(Settings settings) {
    settings.version = 1;
    settings.vertical_fov_degrees = settings.vertical_fov_degrees == kFovGameOriginal
            ? kFovGameOriginal
            : finite_clamp(settings.vertical_fov_degrees, kFovMinimum, kFovMaximum,
                    kFovGameOriginal);
    settings.window_size_preset = std::min<std::uint8_t>(
            settings.window_size_preset, kWindowPresetCount - 1);
    settings.master_volume = finite_clamp(settings.master_volume, 0.0F, 1.0F, 1.0F);
    settings.gamma = finite_clamp(settings.gamma, kGammaMinimum, kGammaMaximum, 1.0F);
    settings.saturation = finite_clamp(
            settings.saturation, kSaturationMinimum, kSaturationMaximum, 1.0F);
    settings.controls = controls::validate(settings.controls);
    return settings;
}

bool same(const Settings& left, const Settings& right) {
    return left.version == right.version && left.vertical_fov_degrees == right.vertical_fov_degrees &&
            left.window_size_preset == right.window_size_preset && left.fullscreen == right.fullscreen &&
            left.widescreen == right.widescreen && left.master_volume == right.master_volume &&
            left.gamma == right.gamma && left.saturation == right.saturation &&
            left.calibrated_weapon_models == right.calibrated_weapon_models &&
            left.controls == right.controls;
}

std::filesystem::path config_path() {
    wchar_t explicit_path[32768]{};
    const DWORD explicit_count = GetEnvironmentVariableW(
            L"XR64_PORT_OPTIONS_CONFIG", explicit_path, 32768);
    if (explicit_count != 0 && explicit_count < 32768) return explicit_path;
    wchar_t local_app_data[MAX_PATH]{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    const std::filesystem::path root = count != 0 && count < MAX_PATH
            ? std::filesystem::path(local_app_data)
            : std::filesystem::temp_directory_path();
    return root / "XR64" / "RageWars" / "port-options-v1.json";
}

bool read_number(const std::string& text, const char* key, double& value) {
    const std::regex expression(std::string("\\\"") + key + "\\\"\\s*:\\s*"
            "([-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][-+]?[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(text, match, expression)) return false;
    try { value = std::stod(match[1].str()); return std::isfinite(value); }
    catch (...) { return false; }
}

bool read_bool(const std::string& text, const char* key, bool& value) {
    const std::regex expression(std::string("\\\"") + key + "\\\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (!std::regex_search(text, match, expression)) return false;
    value = match[1].str() == "true";
    return true;
}

Settings load() {
    std::ifstream file(config_path(), std::ios::binary);
    if (!file) return Settings{};
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    Settings loaded{};
    double number = 0.0;
    bool flag = false;
    if (read_number(text, "verticalFovDegrees", number)) loaded.vertical_fov_degrees = static_cast<float>(number);
    if (read_number(text, "windowSizePreset", number) && number >= 0.0 &&
            number < static_cast<double>(kWindowPresetCount)) {
        loaded.window_size_preset = static_cast<std::uint8_t>(number);
    }
    if (read_bool(text, "fullscreen", flag)) loaded.fullscreen = flag;
    if (read_bool(text, "widescreen", flag)) loaded.widescreen = flag;
    if (read_bool(text, "calibratedWeaponModels", flag)) loaded.calibrated_weapon_models = flag;
    if (read_number(text, "masterVolume", number)) loaded.master_volume = static_cast<float>(number);
    if (read_number(text, "gamma", number)) loaded.gamma = static_cast<float>(number);
    if (read_number(text, "saturation", number)) loaded.saturation = static_cast<float>(number);
    if (read_number(text, "inputProfile", number) && number >= 0 && number <= 1 && std::floor(number) == number)
        loaded.controls.profile = static_cast<controls::Profile>(static_cast<unsigned>(number));
    if (read_number(text, "inputDevice", number) && number >= 0 && number <= 2 && std::floor(number) == number)
        loaded.controls.device_mode = static_cast<controls::DeviceMode>(static_cast<unsigned>(number));
    if (read_number(text, "inputCurve", number) && number >= 0 && number <= 1 && std::floor(number) == number)
        loaded.controls.response_curve = static_cast<controls::ResponseCurve>(static_cast<unsigned>(number));
    if (read_bool(text, "controllerInvertX", flag)) loaded.controls.controller_invert_x = flag;
    if (read_bool(text, "controllerInvertY", flag)) loaded.controls.controller_invert_y = flag;
    if (read_bool(text, "mouseInvertX", flag)) loaded.controls.mouse_invert_x = flag;
    if (read_bool(text, "mouseInvertY", flag)) loaded.controls.mouse_invert_y = flag;
    if (read_number(text, "controllerSensitivityX", number)) loaded.controls.controller_sensitivity_x = static_cast<float>(number);
    if (read_number(text, "controllerSensitivityY", number)) loaded.controls.controller_sensitivity_y = static_cast<float>(number);
    if (read_number(text, "mouseSensitivityX", number)) loaded.controls.mouse_sensitivity_x = static_cast<float>(number);
    if (read_number(text, "mouseSensitivityY", number)) loaded.controls.mouse_sensitivity_y = static_cast<float>(number);
    if (read_number(text, "aimDeadzone", number)) loaded.controls.aim_deadzone = static_cast<float>(number);
    if (read_number(text, "movementThreshold", number)) loaded.controls.movement_threshold = static_cast<float>(number);
    if(read_bool(text,"xrMotionControls",flag))loaded.controls.xr_motion_controls=flag;
    if(read_bool(text,"lookSpring",flag))loaded.controls.look_spring=flag;
    if(read_bool(text,"swapSticks",flag))loaded.controls.swap_sticks=flag;
    for(unsigned i=0;i<bindings::kActionCount;++i)for(unsigned slot=0;slot<2;++slot) {
        const std::string suffix=std::to_string(i)+(slot==0 ? "Primary" : "Alternate");
        if(read_number(text,("keyBind"+suffix).c_str(),number) && number>=0 && number<=65535 && std::floor(number)==number) {
            auto& target=slot==0 ? loaded.controls.keyboard_bindings[i].primary : loaded.controls.keyboard_bindings[i].alternate;
            target=static_cast<bindings::Code>(number);
        }
        if(read_number(text,("padBind"+suffix).c_str(),number) && number>=0 && number<=65535 && std::floor(number)==number) {
            auto& target=slot==0 ? loaded.controls.controller_bindings[i].primary : loaded.controls.controller_bindings[i].alternate;
            target=static_cast<bindings::Code>(number);
        }
    }
    if(read_number(text,"xrTurnSpeed",number))loaded.controls.xr_turn_speed=static_cast<float>(number);
    if(read_number(text,"xrMoveThreshold",number))loaded.controls.xr_move_threshold=static_cast<float>(number);
    if(read_number(text,"xrTriggerThreshold",number))loaded.controls.xr_trigger_threshold=static_cast<float>(number);
    for(unsigned i=0;i<bindings::kActionCount;++i)for(unsigned slot=0;slot<2;++slot) {
        const auto key="xrBind"+std::to_string(i)+(slot==0?"Primary":"Alternate");
        if(read_number(text,key.c_str(),number) && number>=0 && number<=65535 && std::floor(number)==number)
            (slot==0?loaded.controls.xr_bindings[i].primary:loaded.controls.xr_bindings[i].alternate)=static_cast<bindings::Code>(number);
    }
    return validated(loaded);
}

void save_locked() {
    const std::filesystem::path path = config_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        std::fprintf(stderr, "RW_OPTIONS_CONFIG directory_error=%s\n", error.message().c_str());
        return;
    }
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) { std::fprintf(stderr, "RW_OPTIONS_CONFIG write_open_failed\n"); return; }
    file << "{\n"
         << "  \"version\": 1,\n"
         << "  \"verticalFovDegrees\": " << g_settings.vertical_fov_degrees << ",\n"
         << "  \"windowSizePreset\": " << static_cast<unsigned>(g_settings.window_size_preset) << ",\n"
         << "  \"fullscreen\": " << (g_settings.fullscreen ? "true" : "false") << ",\n"
         << "  \"widescreen\": " << (g_settings.widescreen ? "true" : "false") << ",\n"
         << "  \"calibratedWeaponModels\": " << (g_settings.calibrated_weapon_models ? "true" : "false") << ",\n"
         << "  \"masterVolume\": " << g_settings.master_volume << ",\n"
         << "  \"gamma\": " << g_settings.gamma << ",\n"
         << "  \"saturation\": " << g_settings.saturation << ",\n"
         << "  \"inputProfile\": " << static_cast<unsigned>(g_settings.controls.profile) << ",\n"
         << "  \"inputDevice\": " << static_cast<unsigned>(g_settings.controls.device_mode) << ",\n"
         << "  \"inputCurve\": " << static_cast<unsigned>(g_settings.controls.response_curve) << ",\n"
         << "  \"controllerInvertX\": " << (g_settings.controls.controller_invert_x ? "true" : "false") << ",\n"
         << "  \"controllerInvertY\": " << (g_settings.controls.controller_invert_y ? "true" : "false") << ",\n"
         << "  \"mouseInvertX\": " << (g_settings.controls.mouse_invert_x ? "true" : "false") << ",\n"
         << "  \"mouseInvertY\": " << (g_settings.controls.mouse_invert_y ? "true" : "false") << ",\n"
         << "  \"controllerSensitivityX\": " << g_settings.controls.controller_sensitivity_x << ",\n"
         << "  \"controllerSensitivityY\": " << g_settings.controls.controller_sensitivity_y << ",\n"
         << "  \"mouseSensitivityX\": " << g_settings.controls.mouse_sensitivity_x << ",\n"
         << "  \"mouseSensitivityY\": " << g_settings.controls.mouse_sensitivity_y << ",\n"
         << "  \"aimDeadzone\": " << g_settings.controls.aim_deadzone << ",\n"
         << "  \"movementThreshold\": " << g_settings.controls.movement_threshold << ",\n"
         << "  \"lookSpring\": " << (g_settings.controls.look_spring ? "true" : "false") << ",\n"
         << "  \"swapSticks\": " << (g_settings.controls.swap_sticks ? "true" : "false");
    for(unsigned i=0;i<bindings::kActionCount;++i) {
        file << ",\n  \"keyBind" << i << "Primary\": " << g_settings.controls.keyboard_bindings[i].primary
             << ",\n  \"keyBind" << i << "Alternate\": " << g_settings.controls.keyboard_bindings[i].alternate
             << ",\n  \"padBind" << i << "Primary\": " << g_settings.controls.controller_bindings[i].primary
             << ",\n  \"padBind" << i << "Alternate\": " << g_settings.controls.controller_bindings[i].alternate;
    }
    file << ",\n  \"xrMotionControls\": " << (g_settings.controls.xr_motion_controls ? "true" : "false")
         << ",\n  \"xrTurnSpeed\": " << g_settings.controls.xr_turn_speed
         << ",\n  \"xrMoveThreshold\": " << g_settings.controls.xr_move_threshold
         << ",\n  \"xrTriggerThreshold\": " << g_settings.controls.xr_trigger_threshold;
    for(unsigned i=0;i<bindings::kActionCount;++i)
        file << ",\n  \"xrBind" << i << "Primary\": " << g_settings.controls.xr_bindings[i].primary
             << ",\n  \"xrBind" << i << "Alternate\": " << g_settings.controls.xr_bindings[i].alternate;
    file << "\n}\n";
    file.flush();
    if (!file) { std::fprintf(stderr, "RW_OPTIONS_CONFIG write_failed\n"); return; }
    file.close();
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::fprintf(stderr, "RW_OPTIONS_CONFIG replace_failed=%lu\n", GetLastError());
    }
}

void ensure_initialized_locked() {
    if (g_initialized) return;
    g_settings = load();
    g_master_gain.store(g_settings.master_volume, std::memory_order_relaxed);
    g_initialized = true;
    ++g_revision;
}

template <typename Change>
void update(Change&& change) {
    std::lock_guard guard(g_mutex);
    ensure_initialized_locked();
    Settings next = g_settings;
    change(next);
    next = validated(next);
    if (same(next, g_settings)) return;
    g_settings = next;
    g_master_gain.store(next.master_volume, std::memory_order_relaxed); // Publish before persistence.
    ++g_revision;
    save_locked();
}

} // namespace

float master_gain() noexcept { return g_master_gain.load(std::memory_order_relaxed); }
void initialize() { std::lock_guard guard(g_mutex); ensure_initialized_locked(); }
Settings snapshot() { std::lock_guard guard(g_mutex); ensure_initialized_locked(); return g_settings; }
std::uint64_t revision() { std::lock_guard guard(g_mutex); ensure_initialized_locked(); return g_revision; }
void replace(Settings settings) { update([&](Settings& next) { next = settings; }); }
void reset_to_defaults() { replace(Settings{}); }
void set_vertical_fov(float degrees) { update([=](Settings& next) { next.vertical_fov_degrees = degrees; }); }
void set_window_size_preset(std::uint8_t preset) { update([=](Settings& next) { next.window_size_preset = preset; }); }
void set_fullscreen(bool enabled) { update([=](Settings& next) { next.fullscreen = enabled; }); }
void set_widescreen(bool enabled) { update([=](Settings& next) { next.widescreen = enabled; }); }
void set_calibrated_weapon_models(bool enabled) { update([=](Settings& next) { next.calibrated_weapon_models = enabled; }); }
void set_master_volume(float normalized) { update([=](Settings& next) { next.master_volume = normalized; }); }
void set_gamma(float gamma) { update([=](Settings& next) { next.gamma = gamma; }); }
void set_saturation(float saturation) { update([=](Settings& next) { next.saturation = saturation; }); }
void set_controls(controls::Settings settings) { update([=](Settings& next) { next.controls = settings; }); }
void set_binding(bindings::Device device,unsigned action,unsigned slot,bindings::Code code) {
    if(action>=bindings::kActionCount || slot>1 || !bindings::valid_code(device,code) || bindings::reserved_code(device,code))return;
    update([=](Settings& next) {
        auto& set=device==bindings::Device::XR ? next.controls.xr_bindings : device==bindings::Device::Controller ? next.controls.controller_bindings : next.controls.keyboard_bindings;
        bindings::assign(set,action,slot,code);
    });
}
void select_binding(bindings::Device device,unsigned action) {
    std::lock_guard lock(g_mutex);
    g_binding_editor.device=device;
    if(action<bindings::kActionCount)g_binding_editor.action=action;
    ++g_editor_revision;
}
void start_binding_capture(unsigned slot) {
    if(slot>1)return;
    std::lock_guard lock(g_mutex);
    g_binding_editor.start(g_binding_editor.device,g_binding_editor.action,slot);
    ++g_editor_revision;
}
bool capture_binding_input(bindings::Device device,bindings::Code code) {
    std::lock_guard lock(g_mutex);
    ensure_initialized_locked();
    const bool accepted=g_binding_editor.accept(device,code);
    if(accepted) {
        auto& set=device==bindings::Device::XR ? g_settings.controls.xr_bindings : device==bindings::Device::Controller ? g_settings.controls.controller_bindings : g_settings.controls.keyboard_bindings;
        bindings::assign(set,g_binding_editor.action,g_binding_editor.slot,code);
        ++g_revision;
        save_locked();
    }
    if(accepted || g_binding_editor.reserved)++g_editor_revision;
    return accepted;
}
void poll_binding_capture(bool neutral,bool focused) {
    std::lock_guard lock(g_mutex);
    if(g_binding_editor.poll(neutral,focused))++g_editor_revision;
}
void cancel_binding_capture() {
    std::lock_guard lock(g_mutex);
    g_binding_editor.cancel();++g_editor_revision;
}
bool binding_input_blocked() {std::lock_guard lock(g_mutex);return g_binding_editor.blocked();}
bindings::Capture binding_editor_snapshot() {std::lock_guard lock(g_mutex);return g_binding_editor;}
std::uint64_t ui_revision() {
    std::lock_guard lock(g_mutex);ensure_initialized_locked();return g_revision+g_editor_revision;
}


} // namespace xr64::rage_wars::port_options

extern "C" const char* xr64_pc_option_label(std::uint32_t option) {
    using namespace xr64::rage_wars::port_options;
    static thread_local char label[32];
    static constexpr const char* resolutions[] = {
        "960X720", "1280X720", "1920X1080", "2560X1440", "3840X2160"
    };
    const Settings settings = snapshot();
    const auto editor=xr64::rage_wars::port_options::binding_editor_snapshot();
    const auto& bound=editor.device==xr64::rage_wars::bindings::Device::XR ? settings.controls.xr_bindings : editor.device==xr64::rage_wars::bindings::Device::Controller
        ? settings.controls.controller_bindings : settings.controls.keyboard_bindings;
    if(option>=XR64_PC_BIND_ACTION_BASE && option<=XR64_PC_BIND_ACTION_END) {
        const unsigned action=option-XR64_PC_BIND_ACTION_BASE;
        const auto value=xr64::rage_wars::bindings::name(editor.device,bound[action].primary);
        std::snprintf(label,sizeof(label),"%s: %s",xr64::rage_wars::bindings::kActionNames[action],value.c_str());
        return label;
    }
    switch (option) {
        case XR64_PC_OPEN_VR:return "VR";
        case XR64_PC_VR_STARTUP: {
#if defined(XR64_DEMO_BUILD)
            const auto preference = xr64::rage_wars::demo::read_vr_startup_preference();
            const char* name = preference == xr64::rage_wars::recomp::XrStartupPreference::On
                    ? "ON" : preference == xr64::rage_wars::recomp::XrStartupPreference::Off
                            ? "OFF" : "AUTO";
            std::snprintf(label, sizeof(label), "VR STARTUP: %s", name);
#else
            std::snprintf(label, sizeof(label), "VR STARTUP: AUTO");
#endif
            return label;
        }
        case XR64_PC_VR_STATUS: {
#if defined(XR64_DEMO_BUILD)
            if (!g_vr_preference_save_error.empty()) {
                std::snprintf(label, sizeof(label), "PREF SAVE FAILED");
                return label;
            }
            const auto status = xr64::rage_wars::recomp::gate5_xr_transition_status();
            std::snprintf(label, sizeof(label), "%s", status.c_str());
            return label;
#else
            return "PC MODE";
#endif
        }
        case XR64_PC_VR_ACTION: {
#if defined(XR64_DEMO_BUILD)
            using xr64::rage_wars::recomp::XrPresentationMode;
            switch (xr64::rage_wars::recomp::gate5_xr_presentation_mode()) {
                case XrPresentationMode::Desktop: return "ENTER VR";
                case XrPresentationMode::StartingXR: return "VR STARTING";
                case XrPresentationMode::XRRunning: return "RETURN TO PC";
                case XrPresentationMode::StoppingXR: return "RETURNING TO PC";
            }
#endif
            return "ENTER VR";
        }
        case XR64_PC_OPEN_VR_MORE:return "MORE VR OPTIONS";
        case XR64_PC_OPEN_XR_BINDINGS:return "VR CONTROLLER BINDINGS";
        case XR64_PC_XR_MOTION:return settings.controls.xr_motion_controls ? "MOTION CONTROLS: ON" : "MOTION CONTROLS: OFF";
        case XR64_PC_XR_RESET:return "RESET VR CONTROLS";
        case XR64_PC_XR_TURN:std::snprintf(label,sizeof(label),"TURN SPEED: %.0f%%",settings.controls.xr_turn_speed*100);break;
        case XR64_PC_XR_MOVE:std::snprintf(label,sizeof(label),"MOVE THRESHOLD: %.0f%%",settings.controls.xr_move_threshold*100);break;
        case XR64_PC_XR_TRIGGER:std::snprintf(label,sizeof(label),"TRIGGER AT: %.0f%%",settings.controls.xr_trigger_threshold*100);break;
        case XR64_PC_LOOK_SPRING: std::snprintf(label,sizeof(label),"LOOK SPRING: %s",settings.controls.look_spring ? "ON" : "OFF");break;
        case XR64_PC_SWAP_STICKS: std::snprintf(label,sizeof(label),"MOVE STICK: %s",settings.controls.swap_sticks ? "RIGHT" : "LEFT");break;
        case XR64_PC_RESET_KEYS: return "RESET KEYBOARD";
        case XR64_PC_OPEN_KEYBOARD: return "KEYBOARD SETUP";
        case XR64_PC_OPEN_RESPONSE: return "STICK RESPONSE";
        case XR64_PC_OPEN_PAD_BINDINGS: return "CONTROLLER SETUP";
        case XR64_PC_NEXT_BINDINGS: return "NEXT PAGE";
        case XR64_PC_BIND_TITLE:
            return xr64::rage_wars::bindings::kActionNames[editor.action];
        case XR64_PC_BIND_PRIMARY:
        case XR64_PC_BIND_ALTERNATE: {
            const unsigned slot=option==XR64_PC_BIND_PRIMARY ? 0U : 1U;
            if(editor.slot==slot && editor.stage==xr64::rage_wars::bindings::CaptureStage::Release)return "RELEASE ALL INPUTS";
            if(editor.slot==slot && editor.stage==xr64::rage_wars::bindings::CaptureStage::Listening) {
                if(editor.reserved)return "RESERVED - TRY ANOTHER";
                return editor.device==xr64::rage_wars::bindings::Device::XR ? "PRESS INPUT / MENU CANCEL" : editor.device==xr64::rage_wars::bindings::Device::Controller ? "PRESS PAD / BACK CANCEL" : "PRESS KEY / ESC CANCEL";
            }
            const auto code=slot==0 ? bound[editor.action].primary : bound[editor.action].alternate;
            const auto value=xr64::rage_wars::bindings::name(editor.device,code);
            std::snprintf(label,sizeof(label),"%s: %s",slot==0 ? "PRIMARY" : "ALTERNATE",value.c_str());
            break;
        }
        case XR64_PC_BIND_CLEAR_PRIMARY: return "CLEAR PRIMARY";
        case XR64_PC_BIND_CLEAR_ALTERNATE: return "CLEAR ALTERNATE";

        case XR64_PC_OPTION_RESOLUTION:
            std::snprintf(label, sizeof(label), "RESOLUTION: %s",
                    resolutions[settings.window_size_preset]);
            break;
        case XR64_PC_OPTION_DISPLAY_MODE:
            std::snprintf(label, sizeof(label), "DISPLAY: %s",
                    settings.fullscreen ? "BORDERLESS" : "WINDOWED");
            break;
        case XR64_PC_OPTION_ASPECT_RATIO:
            std::snprintf(label, sizeof(label), "ASPECT: %s",
                    settings.widescreen ? "16:9" : "4:3");
            break;
        case XR64_PC_OPTION_FOV:
            if (settings.vertical_fov_degrees == kFovGameOriginal) {
                std::snprintf(label, sizeof(label), "FOV: ORIGINAL");
            } else {
                std::snprintf(label, sizeof(label), "FOV: %.0f",
                        settings.vertical_fov_degrees);
            }
            break;
        case XR64_PC_OPTION_MASTER_VOLUME:
            std::snprintf(label, sizeof(label), "VOLUME: %.0f%%",
                    settings.master_volume * 100.0F);
            break;
        case XR64_PC_WEAPON_MODELS:
            return settings.calibrated_weapon_models ? "MODELS: CALIBRATED" : "MODELS: ORIGINAL";
        case XR64_PC_OPEN_DEVICES: return "INPUT DEVICES";
        case XR64_PC_OPEN_DISPLAY: {
            std::snprintf(label, sizeof(label), "DISPLAY OPTIONS");
            break;
        }
        case XR64_PC_OPEN_CONTROLS: {
            std::snprintf(label, sizeof(label), "CONTROLS");
            break;
        }
        case XR64_PC_OPEN_CONTROLLER: {
            std::snprintf(label, sizeof(label), "CONTROLLER");
            break;
        }
        case XR64_PC_OPEN_MOUSE: {
            std::snprintf(label, sizeof(label), "MOUSE");
            break;
        }
        case XR64_PC_BACK: {
            std::snprintf(label, sizeof(label), "BACK");
            break;
        }
        case XR64_PC_INPUT_PROFILE: {
            std::snprintf(label, sizeof(label), "PROFILE: %s", settings.controls.profile == xr64::rage_wars::controls::Profile::Modern ? "MODERN" : "CLASSIC");
            break;
        }
        case XR64_PC_INPUT_DEVICE: {
            static constexpr const char* devices[] = {"AUTO", "CONTROLLER", "KEYBOARD/MOUSE"};
            std::snprintf(label, sizeof(label), "INPUT: %s", devices[static_cast<unsigned>(settings.controls.device_mode)]);
            break;
        }
        case XR64_PC_INPUT_CURVE: {
            if (settings.controls.profile == xr64::rage_wars::controls::Profile::Classic) return "CURVE: CLASSIC";
            std::snprintf(label, sizeof(label), "CURVE: %s", settings.controls.response_curve == xr64::rage_wars::controls::ResponseCurve::Linear ? "LINEAR" : "PRECISION");
            break;
        }
        case XR64_PC_AIM_DEADZONE: {
            if (settings.controls.profile == xr64::rage_wars::controls::Profile::Classic) return "AIM DEADZONE: CLASSIC";
            std::snprintf(label, sizeof(label), "AIM DEADZONE: %.0f%%", settings.controls.aim_deadzone * 100.0F);
            break;
        }
        case XR64_PC_MOVE_THRESHOLD: {
            if (settings.controls.profile == xr64::rage_wars::controls::Profile::Classic) return "MOVE START: CLASSIC";
            std::snprintf(label, sizeof(label), "MOVE START: %.0f%%", settings.controls.movement_threshold * 100.0F);
            break;
        }
        case XR64_PC_PAD_RESET: {
            std::snprintf(label, sizeof(label), "RESET CONTROLLER");
            break;
        }
        case XR64_PC_MOUSE_RESET: {
            std::snprintf(label, sizeof(label), "RESET MOUSE");
            break;
        }
        case XR64_PC_PAD_SENS_X: {
            std::snprintf(label, sizeof(label), "PAD SENS X: %.2f", settings.controls.controller_sensitivity_x);
            break;
        }
        case XR64_PC_PAD_INVERT_X: {
            std::snprintf(label, sizeof(label), "PAD X: %s", settings.controls.controller_invert_x ? "INVERT" : "NORMAL");
            break;
        }
        case XR64_PC_PAD_SENS_Y: {
            std::snprintf(label, sizeof(label), "PAD SENS Y: %.2f", settings.controls.controller_sensitivity_y);
            break;
        }
        case XR64_PC_PAD_INVERT_Y: {
            std::snprintf(label, sizeof(label), "PAD Y: %s", settings.controls.controller_invert_y ? "INVERT" : "NORMAL");
            break;
        }
        case XR64_PC_MOUSE_SENS_X: {
            std::snprintf(label, sizeof(label), "MOUSE SENS X: %.2f", settings.controls.mouse_sensitivity_x);
            break;
        }
        case XR64_PC_MOUSE_INVERT_X: {
            std::snprintf(label, sizeof(label), "MOUSE X: %s", settings.controls.mouse_invert_x ? "INVERT" : "NORMAL");
            break;
        }
        case XR64_PC_MOUSE_SENS_Y: {
            std::snprintf(label, sizeof(label), "MOUSE SENS Y: %.2f", settings.controls.mouse_sensitivity_y);
            break;
        }
        case XR64_PC_MOUSE_INVERT_Y: {
            std::snprintf(label, sizeof(label), "MOUSE Y: %s", settings.controls.mouse_invert_y ? "INVERT" : "NORMAL");
            break;
        }
        default:
            std::snprintf(label, sizeof(label), "UNKNOWN");
            break;
    }
    return label;
}

extern "C" void xr64_pc_option_cycle(std::uint32_t option) {
    using namespace xr64::rage_wars::port_options;
    const Settings settings = snapshot();
    switch (option) {
        case XR64_PC_OPTION_RESOLUTION:
            set_window_size_preset(static_cast<std::uint8_t>(
                    (settings.window_size_preset + 1) % kWindowPresetCount));
            break;
        case XR64_PC_OPTION_DISPLAY_MODE:
            set_fullscreen(!settings.fullscreen);
            break;
        case XR64_PC_OPTION_ASPECT_RATIO:
            set_widescreen(!settings.widescreen);
            break;
        case XR64_PC_OPTION_FOV: {
            const float next = settings.vertical_fov_degrees == kFovGameOriginal
                    ? kFovMinimum
                    : settings.vertical_fov_degrees >= kFovMaximum
                            ? kFovGameOriginal
                            : settings.vertical_fov_degrees + 5.0F;
            set_vertical_fov(next);
            break;
        }
        case XR64_PC_VR_STARTUP: {
#if defined(XR64_DEMO_BUILD)
            using xr64::rage_wars::recomp::XrStartupPreference;
            auto preference = xr64::rage_wars::demo::read_vr_startup_preference();
            preference = static_cast<XrStartupPreference>(
                    (static_cast<unsigned>(preference) + 1U) % 3U);
            std::string error;
            if (!xr64::rage_wars::demo::save_vr_startup_preference(preference, error)) {
                g_vr_preference_save_error = error;
            } else {
                g_vr_preference_save_error.clear();
            }
            g_vr_menu_revision.fetch_add(1, std::memory_order_relaxed);
#endif
            return;
        }
        case XR64_PC_VR_ACTION: {
#if defined(XR64_DEMO_BUILD)
            xr64_pc_vr_action();
#endif
            return;
        }
        case XR64_PC_VR_STATUS:
        case XR64_PC_OPEN_VR_MORE:
            return;
        case XR64_PC_WEAPON_MODELS:
            set_calibrated_weapon_models(!settings.calibrated_weapon_models);
            return;
        case XR64_PC_OPTION_MASTER_VOLUME: {
            const float decreased = settings.master_volume - 0.1F;
            const float next = settings.master_volume <= 0.001F
                    ? 1.0F : (decreased < 0.0F ? 0.0F : decreased);
            set_master_volume(next);
            break;
        }
        default: {
            auto next = settings.controls;
            using namespace xr64::rage_wars::controls;
            const auto sensitivity_next = [](float value) { return value >= 2.99F ? 0.25F : value + 0.25F; };
            switch (option) {
                case XR64_PC_XR_MOTION:next.xr_motion_controls=!next.xr_motion_controls;break;
                case XR64_PC_XR_TURN:next.xr_turn_speed=next.xr_turn_speed>=0.99F?0.1F:next.xr_turn_speed+0.1F;break;
                case XR64_PC_XR_MOVE:next.xr_move_threshold=next.xr_move_threshold>=0.59F?0.15F:next.xr_move_threshold+0.05F;break;
                case XR64_PC_XR_TRIGGER:next.xr_trigger_threshold=next.xr_trigger_threshold>=0.89F?0.2F:next.xr_trigger_threshold+0.05F;break;
                case XR64_PC_XR_RESET:next.xr_motion_controls=true;next.xr_bindings=xr64::rage_wars::bindings::xr_defaults();next.xr_turn_speed=0.5F;next.xr_move_threshold=0.25F;next.xr_trigger_threshold=0.55F;break;
                case XR64_PC_LOOK_SPRING: next.look_spring=!next.look_spring;break;
                case XR64_PC_SWAP_STICKS: next.swap_sticks=!next.swap_sticks;break;
                case XR64_PC_RESET_KEYS: next.keyboard_bindings=xr64::rage_wars::bindings::keyboard_defaults();break;
                case XR64_PC_BIND_PRIMARY: start_binding_capture(0);return;
                case XR64_PC_BIND_ALTERNATE: start_binding_capture(1);return;
                case XR64_PC_BIND_CLEAR_PRIMARY:
                case XR64_PC_BIND_CLEAR_ALTERNATE: {
                    const auto editor=binding_editor_snapshot();
                    set_binding(editor.device,editor.action,option==XR64_PC_BIND_CLEAR_PRIMARY ? 0U : 1U,0);
                    return;
                }
                case XR64_PC_INPUT_PROFILE: next.profile = next.profile == Profile::Modern ? Profile::Classic : Profile::Modern; break;
                case XR64_PC_INPUT_DEVICE: next.device_mode = static_cast<DeviceMode>((static_cast<unsigned>(next.device_mode) + 1) % 3); break;
                case XR64_PC_INPUT_CURVE: next.profile = xr64::rage_wars::controls::Profile::Modern; next.response_curve = next.response_curve == ResponseCurve::Linear ? ResponseCurve::Precision : ResponseCurve::Linear; break;
                case XR64_PC_AIM_DEADZONE: next.profile = xr64::rage_wars::controls::Profile::Modern; next.aim_deadzone = next.aim_deadzone >= 0.299F ? 0.0F : next.aim_deadzone + 0.01F; break;
                case XR64_PC_MOVE_THRESHOLD: next.profile = xr64::rage_wars::controls::Profile::Modern; next.movement_threshold = next.movement_threshold >= 0.599F ? 0.10F : next.movement_threshold + 0.05F; break;
                case XR64_PC_PAD_SENS_X: next.controller_sensitivity_x = sensitivity_next(next.controller_sensitivity_x); break;
                case XR64_PC_PAD_INVERT_X: next.controller_invert_x = !next.controller_invert_x; break;
                case XR64_PC_PAD_SENS_Y: next.controller_sensitivity_y = sensitivity_next(next.controller_sensitivity_y); break;
                case XR64_PC_PAD_INVERT_Y: next.controller_invert_y = !next.controller_invert_y; break;
                case XR64_PC_PAD_RESET: next.controller_bindings=xr64::rage_wars::bindings::controller_defaults(); next.swap_sticks=false; next.controller_sensitivity_x = next.controller_sensitivity_y = 1.0F; next.controller_invert_x = next.controller_invert_y = false; break;
                case XR64_PC_MOUSE_SENS_X: next.mouse_sensitivity_x = sensitivity_next(next.mouse_sensitivity_x); break;
                case XR64_PC_MOUSE_INVERT_X: next.mouse_invert_x = !next.mouse_invert_x; break;
                case XR64_PC_MOUSE_SENS_Y: next.mouse_sensitivity_y = sensitivity_next(next.mouse_sensitivity_y); break;
                case XR64_PC_MOUSE_INVERT_Y: next.mouse_invert_y = !next.mouse_invert_y; break;
                case XR64_PC_MOUSE_RESET: next.mouse_sensitivity_x = next.mouse_sensitivity_y = 1.0F; next.mouse_invert_x = next.mouse_invert_y = false; break;
                default: return;
            }
            set_controls(next);
            break;
        }
    }
}

extern "C" void xr64_pc_binding_select(std::uint32_t device,std::uint32_t action) {
    if(device>2)return;
    xr64::rage_wars::port_options::select_binding(static_cast<xr64::rage_wars::bindings::Device>(device),action);
}
extern "C" void xr64_pc_binding_cancel() {xr64::rage_wars::port_options::cancel_binding_capture();}

extern "C" void xr64_pc_vr_action() {
#if defined(XR64_DEMO_BUILD)
    using namespace xr64::rage_wars::recomp;
    switch (gate5_xr_presentation_mode()) {
        case XrPresentationMode::Desktop:
            request_gate5_xr_transition(XrTransitionRequest::EnterVR);
            break;
        case XrPresentationMode::XRRunning:
            request_gate5_xr_transition(XrTransitionRequest::ReturnToPC);
            break;
        case XrPresentationMode::StartingXR:
        case XrPresentationMode::StoppingXR:
            break;
    }
    g_vr_menu_revision.fetch_add(1, std::memory_order_relaxed);
#endif
}

extern "C" std::uint64_t xr64_pc_ui_revision() {
    auto revision = xr64::rage_wars::port_options::ui_revision();
#if defined(XR64_DEMO_BUILD)
    revision ^= xr64::rage_wars::recomp::gate5_xr_transition_revision() * 0x9E3779B185EBCA87ULL;
    revision ^= g_vr_menu_revision.load(std::memory_order_relaxed) * 0xC2B2AE3D27D4EB4FULL;
#endif
    return revision;
}
