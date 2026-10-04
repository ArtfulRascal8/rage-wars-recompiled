#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "rage_wars_openxr.hpp"

namespace xr64::rage_wars::demo {

enum class StartupOutcome {
    Ready,
    Help,
    SetupCancelled,
    InvalidArgs,
};

struct Startup {
    std::filesystem::path rom;
    std::filesystem::path data;
    std::filesystem::path controller_pak;
    std::filesystem::path trace;
    std::filesystem::path summary;
    std::filesystem::path input_replay;
    xr64::rage_wars::recomp::XrStartupPreference vr_preference =
            xr64::rage_wars::recomp::XrStartupPreference::Auto;
    std::string stop_checkpoint = "none";
    std::uint64_t max_vi = 0;
    std::uint64_t max_seconds = 0;
    bool headless = false;
    bool visible = true;
    bool presentation_explicit = false;
    bool mute = false;
    bool developer = false;
    bool no_pak = false;
    bool skip_pak_selftest = false;
    bool godot_live_bridge = false;
    bool rw017_peripheral_trace = false;
    bool rw023_force_ares_producer_path = false;
    bool setup_requested = false;
    bool weapon_calibration = false;
    std::filesystem::path weapon_assets;
};

StartupOutcome resolve_startup(
        int argc, wchar_t** argv, Startup& startup, std::string& error);
bool show_launcher(Startup& startup, bool force_setup = false);
std::string_view startup_usage();
xr64::rage_wars::recomp::XrStartupPreference read_vr_startup_preference();
bool save_vr_startup_preference(
        xr64::rage_wars::recomp::XrStartupPreference preference, std::string& error);
void show_error(const char* message);

}
