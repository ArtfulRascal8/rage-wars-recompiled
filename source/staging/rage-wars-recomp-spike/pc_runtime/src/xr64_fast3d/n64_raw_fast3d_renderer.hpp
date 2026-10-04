#pragma once

#include "n64_emulator_render_bridge.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace xr64 {

struct N64RawFast3DColor {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct N64RawFast3DVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
    float s = 0.0F;
    float t = 0.0F;
    N64RawFast3DColor color;
    // Always retain the four bytes stored by G_VTX. With G_LIGHTING those
    // are a signed normal plus alpha, whereas `color` is the resulting shade.
    N64RawFast3DColor attributes;
    // The RSP uses these bytes as either shade RGBA or a normal according to
    // the geometry mode active at G_VTX. Preserve that fact for diagnostics.
    std::uint32_t geometry_mode_at_load = 0;
};

// RSP viewport fields are signed 10.2 fixed point. Their scale/translation
// combination becomes top-origin RDP pixels after division by four.
struct N64RawFast3DViewport {
    std::int16_t scale_x = 640;
    std::int16_t scale_y = 480;
    std::int16_t scale_z = 511;
    std::int16_t translate_x = 640;
    std::int16_t translate_y = 480;
    std::int16_t translate_z = 511;
};

// Full-screen and split-screen scene viewports cover at least half of each
// guest-frame axis. Smaller perspective viewports are menu previews/panels;
// expanding those to the host aspect stretches their clipping region away
// from the surrounding proportional 4:3 menu geometry.
inline bool n64_raw_fast3d_widescreen_viewport_eligible(
        const N64RawFast3DViewport &viewport,
        int logical_width, int logical_height) {
    if (logical_width <= 0 || logical_height <= 0) return false;
    const std::int32_t scale_x = viewport.scale_x;
    const std::int32_t scale_y = viewport.scale_y;
    return std::abs(scale_x) >= logical_width &&
            std::abs(scale_y) >= logical_height;
}

struct N64RawFast3DGuestPosition {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float ndc_x = 0.0F;
    float ndc_y = 0.0F;
    float ndc_z = 0.0F;
};

inline bool n64_raw_fast3d_map_to_guest_viewport(
        const N64RawFast3DVertex &vertex,
        const N64RawFast3DViewport &viewport,
        N64RawFast3DGuestPosition &position) {
    if (vertex.w == 0.0F) return false;
    const float inverse_w = 1.0F / vertex.w;
    position.ndc_x = vertex.x * inverse_w;
    position.ndc_y = vertex.y * inverse_w;
    position.ndc_z = vertex.z * inverse_w;
    position.x = (position.ndc_x * static_cast<float>(viewport.scale_x) +
            static_cast<float>(viewport.translate_x)) / 4.0F;
    position.y = (static_cast<float>(viewport.translate_y) -
            position.ndc_y * static_cast<float>(viewport.scale_y)) / 4.0F;
    position.z = (position.ndc_z * static_cast<float>(viewport.scale_z) +
            static_cast<float>(viewport.translate_z)) / 4.0F;
    return true;
}

// The pixel-space backend uses glOrtho(..., -1, +1), which negates Z.
// Cancel that negation so GL_LESS retains the guest projection depth order.
inline float n64_raw_fast3d_pixel_projection_z(float ndc_z) {
    return -ndc_z;
}

struct N64RawFast3DHostClipPosition {
    float x, y, z, w;
};

// Fold the guest viewport's X/Y placement into homogeneous clip coordinates.
// Keep W until the GPU clips and interpolates. Z retains the guest projection's
// direction; the pixel-space glOrtho path would negate it and reverse GL_LESS.
inline N64RawFast3DHostClipPosition n64_raw_fast3d_map_to_host_clip(
        const N64RawFast3DVertex &vertex, const N64RawFast3DViewport &viewport,
        float logical_width, float logical_height) {
    return {
        (vertex.x * viewport.scale_x + vertex.w * viewport.translate_x) /
                (2.0F * logical_width) - vertex.w,
        vertex.w - (vertex.w * viewport.translate_y - vertex.y * viewport.scale_y) /
                (2.0F * logical_height),
        vertex.z, vertex.w
    };
}

enum class N64RawTextureWrap { Clamp, Repeat, Mirror };
inline N64RawTextureWrap n64_raw_texture_wrap(unsigned mode, unsigned mask, unsigned extent) {
    // GL wrapping is equivalent only when the RDP mask spans the uploaded axis.
    if ((mode & 2U) || mask == 0 || mask > 15 || (1U << mask) != extent)
        return N64RawTextureWrap::Clamp;
    return (mode & 1U) ? N64RawTextureWrap::Mirror : N64RawTextureWrap::Repeat;
}
struct N64RawFast3DTexture {
    bool alpha_precombined = false;
    N64RawTextureWrap wrap_s = N64RawTextureWrap::Clamp;
    N64RawTextureWrap wrap_t = N64RawTextureWrap::Clamp;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t format = 0;
    std::uint32_t size = 2;
    std::vector<std::uint8_t> rgba;
    // Set only after the owning decoded payload has become immutable. A raw
    // decoder texture can leave this unset and be fingerprinted on admission.
    std::uint64_t content_hash = 0;
    bool content_hash_valid = false;
};

inline std::uint64_t n64_raw_fast3d_texture_hash(const N64RawFast3DTexture& t) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto byte = [&hash](std::uint8_t value) {
        hash = (hash ^ value) * 1099511628211ULL;
    };
    const auto word = [&byte](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) byte(static_cast<std::uint8_t>(value >> (i * 8)));
    };
    word(t.width); word(t.height); word(t.format); word(t.size);
    word(static_cast<std::uint32_t>(t.wrap_s));
    word(static_cast<std::uint32_t>(t.wrap_t));
    byte(t.alpha_precombined ? 1U : 0U);
    word(t.rgba.size());
    for (std::uint8_t value : t.rgba) byte(value);
    return hash;
}

struct N64RawFast3DDrawState {
    bool tracked_attachment=false; // Vertices are metres in the calibrated grip/aim frame.
    bool camera_attachment=false; // First-person list stays relative to the current view.
    std::array<float,16> source_projection{}; // Owned world-to-clip basis, before host clipping.
    bool perspective_projection = false;
    float perspective_y_scale = 0.0F;
    float perspective_x_norm = 0.0F;
    float perspective_y_norm = 0.0F;
    float perspective_w_norm = 0.0F;
    bool textured = false;
    bool alpha_blend = false;
    bool depth_test = false;
    bool depth_write = false;
    bool depth_compare = false;
    bool cull_front = false;
    bool cull_back = false;
    std::uint64_t other_mode = 0;
    std::uint32_t combine_word0 = 0;
    std::uint32_t combine_word1 = 0;
    N64RawFast3DColor primitive_color;
    N64RawFast3DColor environment_color;
    N64RawFast3DColor fill_color;
    N64RawFast3DViewport viewport;
    std::uint32_t geometry_mode = 0;
};

// Bounded material supported by the recovered Ares MAG60 RDP witness.
// Cycle 0: RGB=TEXEL0, A=TEXEL0_A*PRIMITIVE_A.
// Cycle 1: RGB=COMBINED, A=COMBINED_A*ENVIRONMENT_A.
// Other encodings deliberately remain on the existing backend path.
inline bool n64_raw_fast3d_texture_rgb_material(const N64RawFast3DDrawState &state) {
    return state.textured && ((state.other_mode >> 52U) & 3U) == 1U &&
            (state.combine_word0 & 0x00FFFFFFU) == 0x00FF97FFU &&
            state.combine_word1 == 0xFF14FE3FU;
}

// Captured flash: RGB=(PRIMITIVE-ENVIRONMENT)*TEXEL0+ENVIRONMENT;
// alpha=TEXEL0_ALPHA*PRIMITIVE_ALPHA. Both encoded cycles are identical.
inline bool n64_raw_fast3d_palette_lerp_material(const N64RawFast3DDrawState &state) {
    return state.textured && ((state.other_mode >> 52U) & 3U) <= 1U &&
        (state.combine_word0 & 0x00FFFFFFU) == 0x00309661U &&
        state.combine_word1 == 0x552EFF7FU;
}

// The captured flare, explosion, and impact sprites use this palette lerp
// combiner with the RDP's bilerp/average texture filter. Keep smoothing
// bounded to that recovered effect material until the backend implements the
// N64 three-point filter for every material.
inline bool n64_raw_fast3d_smooth_palette_lerp_material(
        const N64RawFast3DDrawState &state) {
    const std::uint64_t texture_filter = (state.other_mode >> 44U) & 3U;
    return n64_raw_fast3d_palette_lerp_material(state) &&
            (texture_filter == 2U || texture_filter == 3U);
}

// Captured two-cycle HUD rectangles use TEXEL0 * ENVIRONMENT for RGB and
// TEXEL1 * ENVIRONMENT for alpha. The decoder precombines the I4 mask from
// tile 1 into the uploaded texture alpha; the backend must still source the
// RGB multiplier from environment rather than the unrelated primitive color.
inline bool n64_raw_fast3d_environment_hud_material(
        const N64RawFast3DDrawState &state) {
    return state.textured && ((state.other_mode >> 52U) & 3U) == 1U &&
            state.combine_word0 == 0xFC12ABFFU &&
            state.combine_word1 == 0xFFFFFE38U;
}

inline float n64_raw_fast3d_texture_alpha_scale(const N64RawFast3DDrawState &state) {
    return (static_cast<float>(state.primitive_color.a) / 255.0F) *
            (static_cast<float>(state.environment_color.a) / 255.0F);
}

class N64RawFast3DBackend {
public:
    virtual ~N64RawFast3DBackend() = default;

    virtual bool begin_frame(std::uint32_t width, std::uint32_t height,
            std::string &error) = 0;
    virtual bool set_viewport(int x, int y, int width, int height,
            std::string &error) = 0;
    virtual bool set_scissor(int x, int y, int width, int height,
            std::string &error) = 0;
    virtual bool set_color_image(std::uint32_t address, std::uint32_t width,
            std::string &error) = 0;
    virtual bool set_depth_image(std::uint32_t address,
            std::string &error) = 0;
    virtual bool upload_texture(const N64RawFast3DTexture &texture,
            std::string &error) = 0;
    virtual bool draw_triangles(const std::vector<N64RawFast3DVertex> &vertices,
            const N64RawFast3DDrawState &state, std::string &error) = 0;
    // Owned decoded frames expose a contiguous view. Legacy backends keep the
    // vector entry point until migrated; the GL backend overrides this view.
    virtual bool draw_triangles_view(const N64RawFast3DVertex *vertices,
            std::size_t count, const N64RawFast3DDrawState &state,
            std::string &error) {
        if (count == 0) return draw_triangles({}, state, error);
        return draw_triangles(std::vector<N64RawFast3DVertex>(vertices,
                vertices + count), state, error);
    }
    virtual bool draw_rectangle(float left, float top, float right, float bottom,
            float s0, float t0, float s1, float t1,
            const N64RawFast3DDrawState &state, std::string &error) = 0;
    virtual bool clear_depth(std::string &error) = 0;
    virtual bool end_frame(std::string &error) = 0;
};

// Optional exact-list presentation replacement. Original commands still execute
// to preserve RSP/RDP state; only that list's geometry/uploads are substituted.
struct N64RawFast3DReplacementBatch {
    N64RawFast3DTexture texture;
    std::vector<N64RawFast3DVertex> vertices;
    std::uint32_t combine_word0=0, combine_word1=0;
};
struct N64RawFast3DListReplacement {
    std::uint32_t physical_address=0;
    std::vector<N64RawFast3DReplacementBatch> batches;
    bool matched=false;
    bool tracked_attachment=false; // Vertices are metres in the calibrated grip/aim frame.
    bool camera_attachment=false;
};

struct N64RawFast3DTaskStats {
    std::size_t command_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t rectangle_count = 0;
    bool scissor_set = false;
    int scissor_x = 0;
    int scissor_y = 0;
    int scissor_width = 0;
    int scissor_height = 0;
    bool color_image_set = false;
    std::uint32_t color_image_address = 0;
    std::uint32_t color_image_width = 0;
    std::uint32_t color_image_format = 0;
    std::uint32_t color_image_size = 0;
};

// Executes an authentic Fast3D/F3DEX/F3DEX2 task directly from canonical
// big-endian RDRAM. The backend receives command effects, not RenderSceneData.
bool execute_n64_raw_fast3d_task(
        const N64GraphicsTaskDescriptor &task,
        const std::uint8_t *rdram,
        std::size_t rdram_size,
        N64RawFast3DBackend &backend,
        std::string &error,
        N64RawFast3DTaskStats *stats = nullptr,
        N64RawFast3DListReplacement *replacement = nullptr);

} // namespace xr64
