#include "rage_wars_type1d.hpp"

#include <array>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace xr64::rage_wars::type1d {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'R', 'W', '1', 'D'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kRecordSize = 12;

std::uint32_t read_be32(const std::uint8_t *bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
            (static_cast<std::uint32_t>(bytes[1]) << 16U) |
            (static_cast<std::uint32_t>(bytes[2]) << 8U) |
            static_cast<std::uint32_t>(bytes[3]);
}

[[noreturn]] void invalid(const std::filesystem::path &path, const char *reason) {
    throw std::runtime_error("invalid Rage Wars TYPE-1D data " + path.string() + ": " + reason);
}

void validate_descriptor(const Descriptor &descriptor, std::size_t index,
        const std::filesystem::path &path) {
    constexpr std::array<Descriptor, kDescriptorCount> expected{{
            {0x1D, 0x0041C48C, 0x00000E06},
            {0x1D, 0x0041C32C, 0x00000E03},
            {0x1D, 0x0041C45C, 0x00000001},
            {0x1D, 0x0041C514, 0x00000002},
            {0x1D, 0x0041C4F4, 0x0000000A},
            {0x1D, 0x0041C4EC, 0x00000000},
    }};
    if (descriptor.key != expected[index].key || descriptor.callback != expected[index].callback ||
            descriptor.event != expected[index].event) {
        invalid(path, "descriptor differs from the verified TYPE-1D contract");
    }
}

} // namespace

std::array<Descriptor, kDescriptorCount> load_private_data(
        const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("could not open private Rage Wars TYPE-1D data: " + path.string());
    }
    const std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() != kHeaderSize + kDescriptorCount * kRecordSize) {
        invalid(path, "unexpected size");
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        invalid(path, "bad magic");
    }
    if (read_be32(bytes.data() + 4) != kVersion ||
            read_be32(bytes.data() + 8) != kDescriptorCount) {
        invalid(path, "unsupported version or descriptor count");
    }

    std::array<Descriptor, kDescriptorCount> descriptors{};
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const std::uint8_t *record = bytes.data() + kHeaderSize + index * kRecordSize;
        descriptors[index] = {read_be32(record), read_be32(record + 4), read_be32(record + 8)};
        validate_descriptor(descriptors[index], index, path);
    }
    return descriptors;
}

} // namespace xr64::rage_wars::type1d
