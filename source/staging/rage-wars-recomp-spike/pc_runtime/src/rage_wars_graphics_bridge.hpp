#pragma once

#include "xr64_fast3d/n64_live_fast3d_task_adapter.hpp"
#include "xr64_fast3d/n64_microcode_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace xr64::rage_wars {

// An immutable view of the native recomp's RSP submission. Addresses are guest
// virtual addresses at the Ultramodern boundary; the bridge normalizes them to
// physical RDRAM only for the shared decoder.
struct LiveN64TaskContext {
    uint64_t sequence = 0;
    uint32_t rsp_task_address = 0;
    uint32_t task_data_address = 0;
    uint32_t task_data_size = 0;
    uint32_t microcode_address = 0;
    uint32_t microcode_size = 0;
    uint32_t microcode_data_address = 0;
    uint32_t microcode_data_size = 0;
    const uint8_t *rdram = nullptr;
    size_t rdram_size = 0;
};

struct GraphicsBridgeResult {
    bool translated = false;
    uint64_t sequence = 0;
    ::xr64::N64MicrocodeFamily microcode_family = ::xr64::N64MicrocodeFamily::Unknown;
    uint32_t surface_count = 0;
    uint32_t triangle_count = 0;
    std::string detail;
};

// Backends consume portable decoded scene data. Neither the native recomp nor
// the F3DEX decoder includes a Godot or RT64 dependency.
class IRageWarsRenderBackend {
public:
    virtual ~IRageWarsRenderBackend() = default;
    virtual bool submit_scene(const LiveN64TaskContext &task,
            const ::xr64::RenderSceneData &scene, std::string &error) = 0;
};

// Raw task consumers execute the authentic display-list words directly. This
// is separate from the scene-data debug adapter so RDP/2D work cannot be lost
// while preserving the older geometry-inspection API.
class IRageWarsRawRenderBackend {
public:
    virtual ~IRageWarsRawRenderBackend() = default;
    virtual bool submit_raw_task(const LiveN64TaskContext &context,
            const ::xr64::N64GraphicsTaskDescriptor &task,
            std::string &error) = 0;
};

class RageWarsGraphicsBridge {
public:
    explicit RageWarsGraphicsBridge(IRageWarsRenderBackend *backend = nullptr);
    GraphicsBridgeResult submit(const LiveN64TaskContext &context);
    GraphicsBridgeResult submit_raw(const LiveN64TaskContext &context,
            IRageWarsRawRenderBackend &backend);

private:
    IRageWarsRenderBackend *backend_ = nullptr;
    ::xr64::N64LiveFast3DSegmentState segment_state_{};
};

} // namespace xr64::rage_wars
