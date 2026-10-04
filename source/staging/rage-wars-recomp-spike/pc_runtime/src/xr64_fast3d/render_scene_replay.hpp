#pragma once

#include "render_scene_data.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xr64 {

// Stable, checksummed normalized scene event used by the XR64 workbench. It
// contains decoded geometry/material inputs plus raw supported Fast3D capture
// state, provenance, and confidence. Camera confidence remains unavailable
// until a separately validated interpretation exists.
// Replay does not need a ROM, emulator process, RSP task, or RDP backend.
bool encode_render_scene_event(
        const RenderSceneData &scene,
        std::vector<uint8_t> &bytes,
        std::string &error);

bool decode_render_scene_event(
        const uint8_t *bytes,
        size_t byte_count,
        RenderSceneData &scene,
        std::string &error);

inline bool decode_render_scene_event(
        const std::vector<uint8_t> &bytes,
        RenderSceneData &scene,
        std::string &error) {
    return decode_render_scene_event(bytes.data(), bytes.size(), scene, error);
}

// Result of the bounded, matrix-aware replay pass. The output scene uses
// Godot-friendly normalized device coordinates for vertex positions, while
// this summary retains the captured VI-space bounds and diagnostic counts.
// The pass applies the retained state associated with each triangle when it
// is available. Older normalized events without associations replay through
// the latest state as an explicitly reported compatibility fallback.
struct Fast3DProjectionSummary {
    N64MicrocodeFamily microcode_family = N64MicrocodeFamily::Unknown;
    uint32_t gbi_command_count = 0;
    uint32_t gbi_no_op_command_count = 0;
    uint32_t gbi_passthrough_command_count = 0;
    std::vector<uint8_t> gbi_passthrough_opcodes;
    bool available = false;
    bool camera_interpretation_verified = false;
    bool uses_per_draw_state = false;
    bool uses_latest_draw_state = false;
    bool uses_vertex_load_state = false;
    uint32_t draw_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t modelview_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t projection_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t viewport_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t draw_count = 0;
    uint32_t draw_state_count_used = 0;
    uint32_t transformed_triangle_count = 0;
    uint32_t rejected_triangle_count = 0;
    uint32_t behind_camera_vertex_count = 0;
    // Source triangles that crossed one or more homogeneous clip planes and
    // were emitted as one or more clipped output triangles.
    uint32_t clipped_triangle_count = 0;
    uint32_t culled_triangle_count = 0;
    uint32_t vertex_load_state_vertex_count = 0;
    uint32_t triangle_state_fallback_vertex_count = 0;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    uint32_t transformed_vertex_count = 0;
    uint32_t visible_vertex_count = 0;
    uint32_t clipped_vertex_count = 0;
    uint32_t invalid_vertex_count = 0;
    bool has_screen_bounds = false;
    float min_screen_x = 0.0F;
    float min_screen_y = 0.0F;
    float max_screen_x = 0.0F;
    float max_screen_y = 0.0F;
    std::string reason;
};
// Conservative semantic labels for captured draw ranges. Only explicit
// geometry selected by the caller is currently classified. The remaining
// values reserve stable vocabulary for future evidence-based classifiers;
// this inspection pass never guesses HUD or offscreen intent from ordering.
enum class Fast3DDrawRole : uint8_t {
    Unknown = 0,
    WorldGeometry = 1,
    ScreenSpaceOverlay = 2,
    OffscreenPass = 3,
};

struct Fast3DCameraStateGroup {
    uint32_t projection_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t perspective_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t viewport_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t triangle_count = 0;
    uint32_t vertex_count = 0;
    double selected_vertex_share = 0.0;
    Fast3DDrawRole draw_role = Fast3DDrawRole::Unknown;
};

// Camera/lens evidence extracted from geometry-associated vertex-load state
// in one inclusive draw range. The view-space camera pose is the origin and
// axes of XR64's reconstructed camera-relative geometry. It is not the game's
// world-space camera transform: captured model-view matrices still combine
// camera and object transforms and cannot be factored generically.
struct Fast3DCameraMetadata {
    bool available = false;
    uint32_t draw_first = 0;
    uint32_t draw_last = 0;
    uint32_t selected_triangle_count = 0;
    uint32_t selected_vertex_count = 0;
    uint32_t associated_vertex_state_count = 0;
    uint32_t invalid_vertex_state_count = 0;
    std::vector<Fast3DCameraStateGroup> state_groups;
    uint32_t dominant_group_index = FAST3D_CAPTURE_NO_INDEX;
    double dominant_vertex_share = 0.0;
    bool perspective_parameters_available = false;
    double vertical_fov_degrees = 0.0;
    double aspect_ratio = 0.0;
    double near_plane = 0.0;
    double far_plane = 0.0;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    Fast3DDrawRole selected_draw_role = Fast3DDrawRole::Unknown;
    std::string draw_role_reason;
    bool view_space_camera_pose_available = false;
    std::string view_right_axis = "+X";
    std::string view_up_axis = "+Y";
    std::string view_forward_axis = "-Z";
    bool world_camera_transform_available = false;
    std::string world_camera_transform_reason =
            "modelview_contains_unseparated_camera_and_object_transforms";
    std::string perspective_reason;
    std::string reason;
};

bool inspect_render_scene_fast3d_camera_metadata(
        const RenderSceneData &scene,
        uint32_t first_draw_index,
        uint32_t last_draw_index,
        Fast3DCameraMetadata &metadata,
        std::string &error);


// Reconstructs projection-independent camera/view-space geometry. The N64
// projection stack's load matrix is treated as the perspective/frustum term
// and omitted, while later G_MTX_MUL entries are retained because games such
// as Banjo place camera orientation there. Model-view state is still resolved
// at vertex-load time. This is the boundary needed for independent Godot eye
// cameras; it does not claim that persistent world/object identity is solved.
bool reconstruct_render_scene_fast3d_view_space(
        const RenderSceneData &scene,
        RenderSceneData &view_scene,
        Fast3DProjectionSummary &summary,
        std::string &error);

// Applies the captured model-view and projection matrices associated with
// each triangle, then maps results through the captured N64 viewport.
// `projected_scene` preserves materials but expands triangles so positions
// that were shared under different draw states stay distinct. Positions are
// normalized device coordinates (Y-up for Godot). Camera confidence is
// reported and is not required for this raw-state diagnostic.
bool project_render_scene_fast3d(
        const RenderSceneData &scene,
        RenderSceneData &projected_scene,
        Fast3DProjectionSummary &summary,
        std::string &error);

// Extracts a view-space scene where each accepted triangle belongs to a single
// projection-plus-viewport state signature. Draw associations are non-contiguous
// by design; unsupported triangles are counted explicitly for reporting.
bool project_render_scene_fast3d_camera_state_group(
        const RenderSceneData &scene,
        uint32_t projection_matrix_index,
        uint32_t viewport_index,
        RenderSceneData &group_scene,
        uint32_t &selected_triangle_count,
        uint32_t &selected_vertex_count,
        uint32_t &invalid_vertex_state_triangle_count,
        uint32_t &mixed_state_triangle_count,
        std::string &error);

} // namespace xr64

