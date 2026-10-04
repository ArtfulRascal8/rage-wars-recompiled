#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace xr64 {

// Exact RSP graphics microcode family carried through live decoding and
// ROM-free normalized replay. A family is never inferred by the scene decoder.
enum class N64MicrocodeFamily : uint8_t {
    Unknown = 0,
    Fast3D = 1,
    F3DEX = 2,
    F3DEX2 = 3,
    S2DEX = 4,
};

struct ModelVertex {
    float position_x = 0.0F;
    float position_y = 0.0F;
    float position_z = 0.0F;
    float normal_x = 0.0F;
    float normal_y = 0.0F;
    float normal_z = 0.0F;
    float uv_x = 0.0F;
    float uv_y = 0.0F;
    float color_r = 1.0F;
    float color_g = 1.0F;
    float color_b = 1.0F;
    float color_a = 1.0F;
    uint32_t fast3d_modelview_matrix_index = 0xffffffffU;
    uint32_t fast3d_projection_matrix_index = 0xffffffffU;
    uint32_t fast3d_viewport_index = 0xffffffffU;
    bool fast3d_has_screen_xy_override = false;
    int16_t fast3d_screen_x_s13_2 = 0;
    int16_t fast3d_screen_y_s13_2 = 0;
    bool fast3d_has_screen_z_override = false;
    uint32_t fast3d_screen_z = 0;
};

struct ModelTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba_pixels;
};

// The exact RDP mux words stay available even when the Godot bridge only has
// a safe approximation for the selected combiner mode.
enum class RdpCombinerMode : uint8_t {
    Unspecified = 0,
    TextureShadeModulate = 1,
    TextureReplace = 2,
    ShadeOnly = 3,
    Unsupported = 4,
    TrilinearTextureShade = 5,
    PrimitiveEnvironmentTextureShade = 6,
    PrimitiveEnvironmentShade = 7,
};

struct RdpCombinerState {
    bool captured = false;
    uint32_t word0 = 0;
    uint32_t word1 = 0;
    RdpCombinerMode mode = RdpCombinerMode::Unspecified;
    bool primitive_color_captured = false;
    uint32_t primitive_color = 0xffffffffU;
    bool environment_color_captured = false;
    uint32_t environment_color = 0xffffffffU;
};

// Effective OtherMode_L raster state at a draw. It is retained independently
// of Godot's approximation so depth, coverage, and RDP blender fidelity can
// advance without losing the original command evidence.
struct RdpRasterState {
    bool other_mode_low_captured = false;
    uint32_t other_mode_low = 0;
    bool other_mode_high_captured = false;
    uint32_t other_mode_high = 0;
    uint8_t cycle_type = 0;
    bool z_buffer_enabled = false;
    bool depth_compare = false;
    bool depth_write = false;
    bool alpha_compare = false;
    bool force_blend = false;
    uint8_t coverage_destination = 0;
    bool coverage_times_alpha = false;
    bool alpha_coverage_select = false;
};

struct ModelSurface {
    uint32_t texture_index = 0;
    uint32_t secondary_texture_index = 0xffffffffU;
    uint32_t render_layer = 0;
    bool alpha_blend = false;
    bool alpha_cutout = false;
    bool clamp_s = false;
    bool clamp_t = false;
    bool cull_front = false;
    bool cull_back = false;
    RdpCombinerState rdp_combiner;
    RdpRasterState rdp_raster;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    // One captured Fast3D draw-state index per triangle in `indices`. This
    // remains separate from material batching so a surface may retain the
    // exact state that was active for each emitted triangle.
    std::vector<uint32_t> triangle_draw_indices;
};

// Fast3D state is retained exactly as it appeared in the captured display
// list. It is deliberately separate from ModelVertex: transforming vertices
// or inferring a camera needs microcode- and game-specific validation.
constexpr uint32_t FAST3D_CAPTURE_NO_INDEX = 0xffffffffU;

// Identifies where the retained Fast3D state came from. A live RDRAM task
// snapshot is stronger evidence than a static segment fixture, but neither is
// itself a camera interpretation.
enum class Fast3DStateProvenance : uint8_t {
    Unavailable = 0,
    StaticSegmentData = 1,
    LiveRdramTaskSnapshot = 2,
};

// A value marked Captured was read directly from a supported Fast3D command.
// It has not been inferred, combined into a camera, or validated for every
// microcode/game convention.
enum class Fast3DStateConfidence : uint8_t {
    Unavailable = 0,
    Captured = 1,
};

struct Fast3DMatrixCapture {
    uint32_t command_address = 0;
    uint32_t matrix_address = 0;
    uint8_t parameters = 0;
    bool projection = false;
    bool load = false;
    bool push = false;
    uint32_t previous_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t modelview_stack_depth = 0;
    // N64 Mtx values reconstructed from their big-endian 16.16 components.
    std::array<int32_t, 16> fixed_16_16{};
};

struct Fast3DViewportCapture {
    uint32_t command_address = 0;
    uint32_t viewport_address = 0;
    std::array<int16_t, 4> scale{};
    std::array<int16_t, 4> translate{};
};

struct Fast3DDrawStateCapture {
    uint32_t command_address = 0;
    uint32_t modelview_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t projection_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t viewport_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t modelview_stack_depth = 0;
};

struct Fast3DSemanticCapture {
    N64MicrocodeFamily microcode_family = N64MicrocodeFamily::Unknown;
    uint32_t command_count = 0;
    uint32_t no_op_command_count = 0;
    uint32_t passthrough_command_count = 0;
    std::vector<uint8_t> passthrough_opcodes;
    Fast3DStateProvenance provenance =
            Fast3DStateProvenance::Unavailable;
    Fast3DStateConfidence segment_confidence =
            Fast3DStateConfidence::Unavailable;
    Fast3DStateConfidence matrix_confidence =
            Fast3DStateConfidence::Unavailable;
    Fast3DStateConfidence viewport_confidence =
            Fast3DStateConfidence::Unavailable;
    Fast3DStateConfidence draw_state_confidence =
            Fast3DStateConfidence::Unavailable;
    // Kept unavailable until the camera interpretation is separately proven.
    Fast3DStateConfidence camera_confidence =
            Fast3DStateConfidence::Unavailable;
    bool segment_state_available = false;
    std::array<uint32_t, 32> segment_bases{};
    std::array<uint8_t, 32> segment_loaded{};
    std::vector<Fast3DMatrixCapture> matrices;
    std::vector<Fast3DViewportCapture> viewports;
    std::vector<Fast3DDrawStateCapture> draws;
};

struct RenderSceneData {
    uint32_t flat_reference_texture_index = 0;
    std::vector<ModelTexture> textures;
    std::vector<ModelSurface> surfaces;
    Fast3DSemanticCapture fast3d;
};

} // namespace xr64
