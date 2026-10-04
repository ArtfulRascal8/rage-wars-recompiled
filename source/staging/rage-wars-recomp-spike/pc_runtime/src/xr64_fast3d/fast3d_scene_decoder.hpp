#pragma once

#include "render_scene_data.hpp"
#include "n64_segment_map.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xr64 {

struct DisplayListRoot {
    uint32_t segmented_address = 0;
    uint32_t render_layer = 0;
    bool alpha_blend = false;
    bool alpha_cutout = false;
};

struct ActorDisplayListRoot {
    size_t part_index = 0;
    DisplayListRoot display_list;
};

bool decode_n64_gbi_scene(
        N64MicrocodeFamily microcode_family,
        const N64SegmentMap &segments,
        const std::vector<DisplayListRoot> &roots,
        RenderSceneData &scene,
        std::string &error);

bool decode_n64_gbi_actor_parts(
        N64MicrocodeFamily microcode_family,
        const N64SegmentMap &segments,
        const std::vector<ActorDisplayListRoot> &roots,
        std::vector<RenderSceneData> &part_models,
        std::string &error);

// Explicit original-Fast3D compatibility wrappers for static fixtures and
// callers whose API contract already fixes that dialect.
bool decode_fast3d_scene(
        const N64SegmentMap &segments,
        const std::vector<DisplayListRoot> &roots,
        RenderSceneData &scene,
        std::string &error);

bool decode_fast3d_actor_parts(
        const N64SegmentMap &segments,
        const std::vector<ActorDisplayListRoot> &roots,
        std::vector<RenderSceneData> &part_models,
        std::string &error);

} // namespace xr64
