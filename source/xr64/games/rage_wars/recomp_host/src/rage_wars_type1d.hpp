#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace xr64::rage_wars::type1d {

struct Descriptor {
    std::uint32_t key;
    std::uint32_t callback;
    std::uint32_t event;
};

constexpr std::size_t kDescriptorCount = 6;

std::array<Descriptor, kDescriptorCount> load_private_data(
        const std::filesystem::path &path);

} // namespace xr64::rage_wars::type1d
