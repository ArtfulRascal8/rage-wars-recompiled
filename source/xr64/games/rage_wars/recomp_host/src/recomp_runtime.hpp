#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xr64::rage_wars::recomp {

struct CheckpointResult {
    bool ok = false;
    std::string error;
    std::string summary;
    std::size_t generated_function_count = 0;
    std::string boundary;
    std::uint32_t boundary_address = 0;
    std::array<std::uint8_t, 0x40> graphics_ostask{};
    std::vector<std::uint8_t> graphics_rdram;
};

// Executes the current deterministic native boot slice. The function owns all
// ROM/RDRAM and host-worker lifetime for the duration of the call and returns
// only after every worker has stopped.
CheckpointResult run_boot_checkpoint(const std::filesystem::path &rom_path,
        const std::filesystem::path &type1d_data_path = {});

}  // namespace xr64::rage_wars::recomp
