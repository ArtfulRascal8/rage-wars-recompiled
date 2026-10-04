#pragma once

#include "rage_wars_graphics_bridge.hpp"

#include <cstdint>
#include <string>

namespace xr64::rage_wars {

class GodotLiveFrameBackend final : public IRageWarsRenderBackend {
public:
    GodotLiveFrameBackend();
    ~GodotLiveFrameBackend() override;

    GodotLiveFrameBackend(const GodotLiveFrameBackend &) = delete;
    GodotLiveFrameBackend &operator=(const GodotLiveFrameBackend &) = delete;

    bool valid() const;
    const std::string &initialization_error() const;
    std::uint64_t published_count() const;
    std::uint64_t last_task_sequence() const;

    bool submit_scene(const LiveN64TaskContext &task,
            const ::xr64::RenderSceneData &scene, std::string &error) override;

private:
    void *mapping_handle_ = nullptr;
    void *mapping_view_ = nullptr;
    std::string initialization_error_;
    std::uint64_t published_count_ = 0;
    std::uint64_t last_task_sequence_ = 0;
};

} // namespace xr64::rage_wars