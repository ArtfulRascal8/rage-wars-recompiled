#pragma once

#include "n64_emulator_render_bridge.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace xr64 {

enum class N64GraphicsRoute : uint8_t {
    ViFallback,
    GbiDecoder,
};

struct N64MicrocodeIdentification {
    N64MicrocodeFamily family = N64MicrocodeFamily::Unknown;
    N64GraphicsRoute route = N64GraphicsRoute::ViFallback;
    std::string signature;
};

// Identifies only explicit, human-readable SDK signatures found in the
// microcode instruction/data images. It deliberately does not guess from game
// identity or similar byte patterns. Recognized-but-unsupported families still
// select VI fallback until an XR64 decoder is registered for them.
N64MicrocodeIdentification identify_n64_microcode(
        const std::vector<uint8_t> &instruction_bytes,
        const std::vector<uint8_t> &data_bytes,
        const std::vector<uint8_t> &command_bytes = {});

N64GraphicsRoute route_n64_microcode(N64MicrocodeFamily family);
const char *n64_microcode_family_name(N64MicrocodeFamily family);

} // namespace xr64
