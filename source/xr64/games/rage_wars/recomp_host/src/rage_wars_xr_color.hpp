#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

// Presentation only: the legacy renderer's RGB arithmetic stays unchanged.
namespace xr64::rage_wars::recomp::xr_color {
// Deliberately retained in consumer builds; callers are initialization/first-eye only.
template<class... Args> inline void report(const char* format, Args... args) {
    std::fprintf(stderr, format, args...);
    std::fflush(stderr);
}
constexpr GLenum srgb8_alpha8 = 0x8C43;
constexpr GLenum framebuffer_srgb = 0x8DB9;
constexpr GLenum attachment_color_encoding = 0x8210;
constexpr GLint srgb_encoding = 0x8C40;
// Owner-accepted Quest 3/VDXR color path. Explicit 0/off preserves RGBA8.
inline bool requested(const char* value) {
    return !(value && (std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0));
}
struct Selection {
    std::int64_t format = 0;
    bool srgb = false;
    bool fallback = false;
};
inline Selection select(const std::vector<std::int64_t>& formats, bool opt_in,
                        bool state_supported) {
    bool rgba = false, srgb = false;
    for (const auto format : formats) {
        rgba |= format == GL_RGBA8;
        srgb |= format == srgb8_alpha8;
    }
    if (opt_in && srgb && state_supported) return {srgb8_alpha8, true, false};
    return {rgba ? GL_RGBA8 : 0, false, opt_in && rgba};
}
inline const char* name(std::int64_t format) {
    if (format == GL_RGBA8) return "GL_RGBA8";
    if (format == srgb8_alpha8) return "GL_SRGB8_ALPHA8";
    return "other";
}

// Query incoming state for each eye. Encoded RGB must not be encoded again.
// Disabling also preserves legacy blending arithmetic. The scope includes the
// mirror blit and restores the caller's real incoming state.
class EncodedEyeScope {
public:
    explicit EncodedEyeScope(bool active) : active_(active),
        incoming_(active ? glIsEnabled(framebuffer_srgb) : GL_FALSE) {
        if (active_) glDisable(framebuffer_srgb);
    }
    ~EncodedEyeScope() {
        if (active_) {
            if (incoming_) glEnable(framebuffer_srgb);
            else glDisable(framebuffer_srgb);
        }
    }
    EncodedEyeScope(const EncodedEyeScope&) = delete;
    EncodedEyeScope& operator=(const EncodedEyeScope&) = delete;
private:
    bool active_;
    GLboolean incoming_;
};
}
