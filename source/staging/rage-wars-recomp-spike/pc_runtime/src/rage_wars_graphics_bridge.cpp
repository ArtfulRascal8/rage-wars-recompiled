#include "xr64_fast3d/render_diagnostics.hpp"
#include "rage_wars_graphics_bridge.hpp"

#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace xr64::rage_wars {
namespace {

constexpr uint32_t kPhysicalMask = 0x1FFFFFFFU;

uint32_t physical_address(uint32_t guest_address) {
    return guest_address & kPhysicalMask;
}

class NativeRdramReader final : public ::xr64::N64MemoryReader {
public:
    NativeRdramReader(const uint8_t *rdram, size_t rdram_size) :
            rdram_(rdram), rdram_size_(rdram_size) {}

    bool read(::xr64::N64MemorySpace space, uint32_t address, size_t size,
            std::vector<uint8_t> &bytes, std::string &error) const override {
        if (space != ::xr64::N64MemorySpace::Rdram) {
            error = "native_recomp_dmem_imem_not_exposed";
            return false;
        }
        if (rdram_ == nullptr || address > rdram_size_ || size > rdram_size_ - address) {
            error = "native_recomp_rdram_range_invalid";
            return false;
        }
        bytes.resize(size);
        for (size_t index = 0; index < size; ++index) {
            bytes[index] = rdram_[(static_cast<size_t>(address) + index) ^ 3U];
        }
        return true;
    }

private:
    const uint8_t *rdram_ = nullptr;
    size_t rdram_size_ = 0;
};

uint32_t count_triangles(const ::xr64::RenderSceneData &scene) {
    uint64_t triangles = 0;
    for (const ::xr64::ModelSurface &surface : scene.surfaces) {
        triangles += surface.indices.size() / 3U;
    }
    return triangles > std::numeric_limits<uint32_t>::max() ?
            std::numeric_limits<uint32_t>::max() : static_cast<uint32_t>(triangles);
}

} // namespace

RageWarsGraphicsBridge::RageWarsGraphicsBridge(IRageWarsRenderBackend *backend) :
        backend_(backend) {}

GraphicsBridgeResult RageWarsGraphicsBridge::submit(const LiveN64TaskContext &context) {
    GraphicsBridgeResult result;
    result.sequence = context.sequence;
    NativeRdramReader memory(context.rdram, context.rdram_size);

    const uint32_t data_address = physical_address(context.task_data_address);
    const uint32_t microcode_address = physical_address(context.microcode_address);
    const uint32_t microcode_data_address = physical_address(context.microcode_data_address);
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr,
            "RW018_RENDER_STAGE bridge_enter rdram_host=%p rdram_size=%zu "
            "dl_physical=0x%08X dl_size=%u ucode_physical=0x%08X ucode_size=%u "
            "ucode_data_physical=0x%08X ucode_data_size=%u\n",
            static_cast<const void *>(context.rdram), context.rdram_size,
            data_address, context.task_data_size, microcode_address,
            context.microcode_size, microcode_data_address,
            context.microcode_data_size);
    std::fflush(stderr);
    std::vector<uint8_t> microcode;
    std::vector<uint8_t> microcode_data;
    std::vector<uint8_t> commands;
    std::string error;
    if (!memory.read(::xr64::N64MemorySpace::Rdram, microcode_address,
                    context.microcode_size, microcode, error) ||
            !memory.read(::xr64::N64MemorySpace::Rdram, microcode_data_address,
                    context.microcode_data_size, microcode_data, error) ||
            !memory.read(::xr64::N64MemorySpace::Rdram, data_address,
                    context.task_data_size, commands, error)) {
        result.detail = std::move(error);
        return result;
    }
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr, "RW018_RENDER_STAGE task_sources_read_complete\n");
    std::fflush(stderr);

    const ::xr64::N64MicrocodeIdentification identification =
            ::xr64::identify_n64_microcode(microcode, microcode_data, commands);
    result.microcode_family = identification.family;
    if (identification.route != ::xr64::N64GraphicsRoute::GbiDecoder) {
        result.detail = "microcode_not_routed:" + identification.signature;
        return result;
    }
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr, "RW018_RENDER_STAGE microcode_identified family=%s signature=%s\n",
            ::xr64::n64_microcode_family_name(identification.family),
            identification.signature.c_str());
    std::fflush(stderr);

    ::xr64::N64GraphicsTaskDescriptor task;
    task.sequence = context.sequence;
    task.task_data_address = data_address;
    task.task_data_size = context.task_data_size;
    task.microcode_address = microcode_address;
    task.microcode_size = context.microcode_size;
    task.microcode_data_address = microcode_data_address;
    task.microcode_data_size = context.microcode_data_size;
    task.microcode_family = identification.family;

    ::xr64::RenderSceneData scene;
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr, "RW018_RENDER_STAGE fast3d_translate_begin snapshot_limit=0x%zX\n",
            static_cast<size_t>(8U * 1024U * 1024U));
    std::fflush(stderr);
    if (!::xr64::translate_n64_live_fast3d_task(memory, task, segment_state_, scene, error)) {
        result.detail = "fast3d_translate_failed:" + error;
        return result;
    }
    result.surface_count = static_cast<uint32_t>(scene.surfaces.size());
    result.triangle_count = count_triangles(scene);
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr, "RW018_RENDER_STAGE fast3d_decode_complete surfaces=%u triangles=%u\n",
            result.surface_count, result.triangle_count);
    std::fflush(stderr);
    if (backend_ != nullptr && !backend_->submit_scene(context, scene, error)) {
        result.detail = "backend_submit_failed:" + error;
        return result;
    }
    result.translated = true;
    result.detail = "live_f3dex_decoded:" + identification.signature;
    return result;
}

GraphicsBridgeResult RageWarsGraphicsBridge::submit_raw(
        const LiveN64TaskContext &context,
        IRageWarsRawRenderBackend &backend) {
    GraphicsBridgeResult result;
    result.sequence = context.sequence;
    NativeRdramReader memory(context.rdram, context.rdram_size);
    const std::uint32_t data_address = physical_address(context.task_data_address);
    const std::uint32_t microcode_address = physical_address(context.microcode_address);
    const std::uint32_t microcode_data_address = physical_address(context.microcode_data_address);
    std::vector<std::uint8_t> microcode;
    std::vector<std::uint8_t> microcode_data;
    std::vector<std::uint8_t> commands;
    std::string error;
    if (!memory.read(::xr64::N64MemorySpace::Rdram, microcode_address,
                    context.microcode_size, microcode, error) ||
            !memory.read(::xr64::N64MemorySpace::Rdram, microcode_data_address,
                    context.microcode_data_size, microcode_data, error) ||
            !memory.read(::xr64::N64MemorySpace::Rdram, data_address,
                    context.task_data_size, commands, error)) {
        result.detail = std::move(error);
        return result;
    }
    const ::xr64::N64MicrocodeIdentification identification =
            ::xr64::identify_n64_microcode(microcode, microcode_data, commands);
    result.microcode_family = identification.family;
    if (identification.route != ::xr64::N64GraphicsRoute::GbiDecoder) {
        result.detail = "microcode_not_routed:" + identification.signature;
        return result;
    }
    ::xr64::N64GraphicsTaskDescriptor task;
    task.sequence = context.sequence;
    task.task_data_address = data_address;
    task.task_data_size = context.task_data_size;
    task.microcode_address = microcode_address;
    task.microcode_size = context.microcode_size;
    task.microcode_data_address = microcode_data_address;
    task.microcode_data_size = context.microcode_data_size;
    task.microcode_family = identification.family;
    if (::xr64::render_diagnostics::legacy()) std::fprintf(stderr,
            "RW020_RAW_TASK_ACCEPTED task_guest=0x%08X dl_physical=0x%08X "
            "family=%s rdram_size=%zu\n",
            context.rsp_task_address, data_address,
            ::xr64::n64_microcode_family_name(identification.family),
            context.rdram_size);
    std::fflush(stderr);
    if (!backend.submit_raw_task(context, task, error)) {
        result.detail = "raw_renderer_failed:" + error;
        return result;
    }
    result.translated = true;
    result.detail = "raw_gbi_executed:" + identification.signature;
    return result;
}

} // namespace xr64::rage_wars
