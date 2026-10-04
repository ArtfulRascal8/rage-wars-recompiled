#include "rage_wars_port_options.hpp"
#include "rage_wars_native_pc_option_ids.h"

#include <Windows.h>

#include <cassert>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

extern "C" const char* xr64_pc_option_label(std::uint32_t option);
extern "C" void xr64_pc_option_cycle(std::uint32_t option);

int main() {
    wchar_t directory[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, directory) != 0);
    const std::wstring path = std::wstring(directory) + L"xr64-port-options-test.json";
    DeleteFileW(path.c_str());
    assert(SetEnvironmentVariableW(L"XR64_PORT_OPTIONS_CONFIG", path.c_str()) != 0);

    using namespace xr64::rage_wars::port_options;
    Settings values{};
    values.vertical_fov_degrees = 500.0F;
    values.window_size_preset = 99;
    values.master_volume = -1.0F;
    values.gamma = std::numeric_limits<float>::infinity();
    values.saturation = std::numeric_limits<float>::quiet_NaN();
    replace(values);
    std::ifstream persistence(path, std::ios::binary);
    const std::string persisted((std::istreambuf_iterator<char>(persistence)), {});
    assert(persisted.find("\"verticalFovDegrees\": 110") != std::string::npos);
    assert(persisted.find("\"masterVolume\": 0") != std::string::npos);
    const Settings checked = snapshot();
    assert(checked.vertical_fov_degrees == kFovMaximum);
    assert(checked.window_size_preset == kWindowPresetCount - 1);
    assert(checked.master_volume == 0.0F);
    assert(checked.gamma == 1.0F);
    assert(checked.saturation == 1.0F);
    set_vertical_fov(kFovGameOriginal);
    assert(snapshot().vertical_fov_degrees == kFovGameOriginal);
    reset_to_defaults();
    const Settings defaults = snapshot();
    assert(defaults.master_volume == 1.0F && defaults.gamma == 1.0F &&
            defaults.saturation == 1.0F);
    assert(std::string(xr64_pc_option_label(XR64_PC_OPTION_RESOLUTION)) ==
            "RESOLUTION: 960X720");
    xr64_pc_option_cycle(XR64_PC_OPTION_RESOLUTION);
    assert(snapshot().window_size_preset == 1);
    xr64_pc_option_cycle(XR64_PC_OPTION_DISPLAY_MODE);
    assert(snapshot().fullscreen);
    xr64_pc_option_cycle(XR64_PC_OPTION_ASPECT_RATIO);
    assert(!snapshot().widescreen);
    xr64_pc_option_cycle(XR64_PC_OPTION_FOV);
    assert(snapshot().vertical_fov_degrees == kFovMinimum);
    xr64_pc_option_cycle(XR64_PC_OPTION_MASTER_VOLUME);
    assert(snapshot().master_volume > 0.89F && snapshot().master_volume < 0.91F);
    assert(std::string(xr64_pc_option_label(XR64_PC_OPTION_MASTER_VOLUME)) ==
            "VOLUME: 90%");
    reset_to_defaults();
    DeleteFileW(path.c_str());
    return 0;
}
