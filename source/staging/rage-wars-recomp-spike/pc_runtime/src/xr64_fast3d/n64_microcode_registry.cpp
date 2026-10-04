#include "n64_microcode_registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace xr64 {
namespace {

struct TextSignature {
    const char *token;
    N64MicrocodeFamily family;
    const char *canonical_name;
};

// Longest/specific signatures must come first.
constexpr std::array<TextSignature, 6> signatures = {{
        {"F3DEX2", N64MicrocodeFamily::F3DEX2, "F3DEX2"},
        {"S2DEX2", N64MicrocodeFamily::S2DEX, "S2DEX2"},
        {"F3DEX", N64MicrocodeFamily::F3DEX, "F3DEX"},
        {"S2DEX", N64MicrocodeFamily::S2DEX, "S2DEX"},
        {"FAST3D", N64MicrocodeFamily::Fast3D, "Fast3D"},
        {"F3D", N64MicrocodeFamily::Fast3D, "F3D"},
}};

uint32_t gliden64_strict_crc_from_emulated_bytes(
        const std::vector<uint8_t> &bytes) {
    if (bytes.size() != 4096U) return 0U;
    uint32_t value = 0xffffffffU;
    for (size_t index = 0; index < bytes.size(); ++index) {
        value ^= bytes[index ^ 3U];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            value = (value >> 1U) ^
                    ((value & 1U) != 0 ? 0xedb88320U : 0U);
        }
    }
    return ~value;
}

bool ascii_token_boundary(uint8_t value) {
    return !std::isalnum(static_cast<unsigned char>(value)) && value != '_';
}

bool contains_signature(
        const std::vector<uint8_t> &bytes,
        const char *token) {
    const size_t token_size = std::char_traits<char>::length(token);
    if (token_size == 0 || bytes.size() < token_size) return false;
    for (size_t offset = 0; offset <= bytes.size() - token_size; ++offset) {
        bool match = true;
        for (size_t index = 0; index < token_size; ++index) {
            const uint8_t upper = static_cast<uint8_t>(std::toupper(
                    static_cast<unsigned char>(bytes[offset + index])));
            if (upper != static_cast<uint8_t>(token[index])) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        const bool left_boundary = offset == 0 ||
                ascii_token_boundary(bytes[offset - 1U]);
        const size_t end = offset + token_size;
        const bool right_boundary = end == bytes.size() ||
                ascii_token_boundary(bytes[end]);
        if (left_boundary && right_boundary) return true;
    }
    return false;
}


bool has_f3dex2_command_abi(const std::vector<uint8_t> &bytes) {
    // F3DEX2 is source-compatible with F3DEX but not binary-compatible. Its
    // matrix, display-list, and end-list opcodes are DA, DE, and DF instead
    // of the original dialect's 01, 06, and B8. Requiring all three avoids
    // promoting a family-name hint or an isolated data byte into a decoder
    // compatibility claim.
    bool has_matrix = false;
    bool has_display_list = false;
    bool has_end_display_list = false;
    for (size_t offset = 0; offset + 8U <= bytes.size(); offset += 8U) {
        switch (bytes[offset]) {
            case 0xdaU: has_matrix = true; break;
            case 0xdeU: has_display_list = true; break;
            case 0xdfU: has_end_display_list = true; break;
            default: break;
        }
    }
    return has_matrix && has_display_list && has_end_display_list;
}
} // namespace

N64GraphicsRoute route_n64_microcode(N64MicrocodeFamily family) {
    // XR64 has explicit dialects for original Fast3D, F3DEX, and F3DEX2.
    // Sprite/custom families remain on VI fallback until their own command
    // tables exist; they are never routed through a similar-looking decoder.
    return (family == N64MicrocodeFamily::Fast3D ||
                   family == N64MicrocodeFamily::F3DEX ||
                   family == N64MicrocodeFamily::F3DEX2) ?
            N64GraphicsRoute::GbiDecoder :
            N64GraphicsRoute::ViFallback;
}

N64MicrocodeIdentification identify_n64_microcode(
        const std::vector<uint8_t> &instruction_bytes,
        const std::vector<uint8_t> &data_bytes,
        const std::vector<uint8_t> &command_bytes) {
    // GLideN64's maintained microcode table identifies this strict 4 KiB
    // checksum as the original Super Mario 64 F3D program. GLideN64 hashes
    // word-swapped host RDRAM; XR64 receives emulated big-endian byte order.
    // The fingerprint is metadata only; no upstream code or ROM bytes ship.
    if (instruction_bytes.size() == 4096U &&
            gliden64_strict_crc_from_emulated_bytes(instruction_bytes) ==
                    0x6932365fU) {
        return {N64MicrocodeFamily::Fast3D,
                N64GraphicsRoute::GbiDecoder, "F3D CRC 6932365f"};
    }
    if ((contains_signature(data_bytes, "F3DEX.NON") ||
                contains_signature(instruction_bytes, "F3DEX.NON")) &&
            (contains_signature(data_bytes, "FIFO 2.05") ||
                contains_signature(instruction_bytes, "FIFO 2.05"))) {
        return {N64MicrocodeFamily::F3DEX2,
                N64GraphicsRoute::GbiDecoder,
                "F3DEX2 GBI2 banner (F3DEX.NoN fifo 2.05)"};
    }
    if (has_f3dex2_command_abi(command_bytes)) {
        return {N64MicrocodeFamily::F3DEX2,
                N64GraphicsRoute::GbiDecoder,
                "F3DEX2 command ABI (DA/DE/DF)"};
    }
    for (const TextSignature &signature : signatures) {
        if (contains_signature(data_bytes, signature.token) ||
                contains_signature(instruction_bytes, signature.token)) {
            return {
                    signature.family,
                    route_n64_microcode(signature.family),
                    signature.canonical_name};
        }
    }
    return {};
}

const char *n64_microcode_family_name(N64MicrocodeFamily family) {
    switch (family) {
        case N64MicrocodeFamily::Fast3D: return "Fast3D";
        case N64MicrocodeFamily::F3DEX: return "F3DEX";
        case N64MicrocodeFamily::F3DEX2: return "F3DEX2";
        case N64MicrocodeFamily::S2DEX: return "S2DEX";
        case N64MicrocodeFamily::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace xr64
