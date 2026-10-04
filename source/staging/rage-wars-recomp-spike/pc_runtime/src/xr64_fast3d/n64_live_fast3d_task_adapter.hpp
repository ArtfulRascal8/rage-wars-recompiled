#pragma once

#include "n64_emulator_render_bridge.hpp"

#include <array>
#include <string>

namespace xr64 {

struct N64LiveFast3DSegmentState {
    std::array<uint32_t, 32> bases{};
    std::array<bool, 32> loaded{};
};


// Takes one synchronous, bounded RDRAM snapshot at the RSP task boundary,
// discovers original Fast3D segment-base commands, and decodes from the copy.
// Unsupported or malformed tasks fail closed so VI compatibility presentation
// can continue without guessing.
bool translate_n64_live_fast3d_task(
        const N64MemoryReader &memory,
        const N64GraphicsTaskDescriptor &task,
        N64LiveFast3DSegmentState &segment_state,
        RenderSceneData &scene,
        std::string &error);



} // namespace xr64
