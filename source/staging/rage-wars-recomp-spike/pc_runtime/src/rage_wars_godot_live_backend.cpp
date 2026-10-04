#include "rage_wars_godot_live_backend.hpp"

#include "rage_wars_live_frame_channel.hpp"
#include "xr64_fast3d/render_scene_replay.hpp"

#include <Windows.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace xr64::rage_wars {

GodotLiveFrameBackend::GodotLiveFrameBackend() {
    static_assert(live_frame::kMappingBytes <=
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()));
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
            PAGE_READWRITE, 0, static_cast<DWORD>(live_frame::kMappingBytes),
            live_frame::kMappingName);
    if (mapping == nullptr) {
        initialization_error_ = "CreateFileMappingW failed:" +
                std::to_string(GetLastError());
        return;
    }
    void *view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
            live_frame::kMappingBytes);
    if (view == nullptr) {
        initialization_error_ = "MapViewOfFile failed:" +
                std::to_string(GetLastError());
        CloseHandle(mapping);
        return;
    }

    mapping_handle_ = mapping;
    mapping_view_ = view;
    std::memset(mapping_view_, 0, live_frame::kMappingBytes);
    auto *frame_header = static_cast<live_frame::Header *>(mapping_view_);
    frame_header->magic = live_frame::kMagic;
    frame_header->version = live_frame::kVersion;
    frame_header->header_size = sizeof(live_frame::Header);
    frame_header->payload_capacity = live_frame::kMaximumEventBytes;
}

GodotLiveFrameBackend::~GodotLiveFrameBackend() {
    if (mapping_view_ != nullptr) UnmapViewOfFile(mapping_view_);
    if (mapping_handle_ != nullptr) CloseHandle(static_cast<HANDLE>(mapping_handle_));
}

bool GodotLiveFrameBackend::valid() const {
    return mapping_view_ != nullptr;
}

const std::string &GodotLiveFrameBackend::initialization_error() const {
    return initialization_error_;
}

std::uint64_t GodotLiveFrameBackend::published_count() const {
    return published_count_;
}

std::uint64_t GodotLiveFrameBackend::last_task_sequence() const {
    return last_task_sequence_;
}

bool GodotLiveFrameBackend::submit_scene(const LiveN64TaskContext &task,
        const ::xr64::RenderSceneData &scene, std::string &error) {
    if (!valid()) {
        error = initialization_error_.empty() ?
                "godot_live_frame_channel_unavailable" : initialization_error_;
        return false;
    }

    std::vector<std::uint8_t> encoded;
    if (!::xr64::encode_render_scene_event(scene, encoded, error)) return false;
    if (encoded.empty() || encoded.size() > live_frame::kMaximumEventBytes) {
        error = "godot_live_scene_event_size_invalid";
        return false;
    }

    std::uint64_t vertex_count = 0;
    std::uint64_t index_count = 0;
    std::uint64_t triangle_count = 0;
    for (const ::xr64::ModelSurface &surface : scene.surfaces) {
        vertex_count += surface.vertices.size();
        index_count += surface.indices.size();
        triangle_count += surface.indices.size() / 3U;
    }
    if (triangle_count > (std::numeric_limits<std::uint32_t>::max)()) {
        error = "godot_live_triangle_count_overflow";
        return false;
    }

    auto *frame_header = static_cast<live_frame::Header *>(mapping_view_);
    const bool rw069_audit_frame = std::getenv("RW069_GEOMETRY_AUDIT") != nullptr &&
            std::getenv("RW069_GEOMETRY_AUDIT")[0] == '1' &&
            published_count_ == 0;
    if (rw069_audit_frame) {
        std::fprintf(stderr,
                "RW069_STAGE_A frame_id=%llu generation_id=%llu surface_count=%zu vertex_count=%llu index_count=%llu triangle_count=%llu payload_bytes=%zu\n",
                static_cast<unsigned long long>(task.sequence),
                static_cast<unsigned long long>(frame_header->publication_generation),
                scene.surfaces.size(),
                static_cast<unsigned long long>(vertex_count),
                static_cast<unsigned long long>(index_count),
                static_cast<unsigned long long>(triangle_count),
                encoded.size());
        std::size_t first_vertex = 0;
        std::size_t first_index = 0;
        for (std::size_t surface_index = 0; surface_index < scene.surfaces.size(); ++surface_index) {
            const auto &surface = scene.surfaces[surface_index];
            std::fprintf(stderr,
                    "RW069_STAGE_A_SURFACE surface_index=%zu first_vertex=%zu vertex_count=%zu first_index=%zu index_count=%zu\n",
                    surface_index, first_vertex, surface.vertices.size(),
                    first_index, surface.indices.size());
            first_vertex += surface.vertices.size();
            first_index += surface.indices.size();
        }
    }
    auto *generation = reinterpret_cast<volatile LONG64 *>(
            &frame_header->publication_generation);
    InterlockedIncrement64(generation);
    std::memcpy(live_frame::payload(frame_header), encoded.data(), encoded.size());
    frame_header->task_sequence = task.sequence;
    frame_header->event_byte_count = static_cast<std::uint32_t>(encoded.size());
    frame_header->surface_count = static_cast<std::uint32_t>(scene.surfaces.size());
    frame_header->triangle_count = static_cast<std::uint32_t>(triangle_count);
    frame_header->vertex_count = static_cast<std::uint32_t>(vertex_count);
    frame_header->index_count = static_cast<std::uint32_t>(index_count);
    MemoryBarrier();
    InterlockedIncrement64(generation);
    if (rw069_audit_frame) {
        std::fprintf(stderr,
                "RW069_STAGE_B published_frame_id=%llu published_generation_id=%llu published_surface_count=%u published_vertex_count=%u published_index_count=%u published_triangle_count=%u published_payload_bytes=%u\n",
                static_cast<unsigned long long>(frame_header->task_sequence),
                static_cast<unsigned long long>(frame_header->publication_generation),
                frame_header->surface_count, frame_header->vertex_count,
                frame_header->index_count, frame_header->triangle_count,
                frame_header->event_byte_count);
    }

    ++published_count_;
    last_task_sequence_ = task.sequence;
    return true;
}

} // namespace xr64::rage_wars