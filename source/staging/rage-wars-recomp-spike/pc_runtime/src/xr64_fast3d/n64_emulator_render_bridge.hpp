#pragma once

#include "render_scene_data.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xr64 {

enum class N64MemorySpace : uint8_t {
    Rdram,
    Dmem,
    Imem,
};

enum class N64ViPixelFormat : uint8_t {
    Rgba8888,
    Rgba5551,
};

struct N64GraphicsTaskDescriptor {
    uint64_t sequence = 0;
    uint32_t task_data_address = 0;
    uint32_t task_data_size = 0;
    uint32_t microcode_address = 0;
    uint32_t microcode_size = 0;
    uint32_t microcode_data_address = 0;
    uint32_t microcode_data_size = 0;
    uint32_t vi_origin = 0;
    uint32_t vi_width = 0;
    N64MicrocodeFamily microcode_family = N64MicrocodeFamily::Unknown;
};

struct N64ViFrame {
    uint64_t sequence = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t row_stride_bytes = 0;
    N64ViPixelFormat pixel_format = N64ViPixelFormat::Rgba8888;
    std::vector<uint8_t> pixels;
};

enum class N64RdpResourceKind : uint8_t {
    TextureImage,
    ColorImage,
    DepthImage,
};

struct N64RdpResourceReference {
    N64RdpResourceKind kind = N64RdpResourceKind::TextureImage;
    uint32_t address = 0;
    uint16_t width = 0;
    uint8_t format = 0;
    uint8_t pixel_size = 0;
    uint32_t row_byte_hint = 0;
};

struct N64MemoryPageSnapshot {
    N64MemorySpace memory = N64MemorySpace::Rdram;
    uint32_t address = 0;
    std::vector<uint8_t> bytes;
};
// A self-contained snapshot of an RDP command range. source_pages preserves
// the aligned 4 KiB source pages as they existed when the RDP submitted them;
// that makes later archaeology independent of the emulator continuing to run.
struct N64RawRdpCapture {
    uint64_t sequence = 0;
    uint32_t start_address = 0;
    uint32_t end_address = 0;
    uint32_t source_page_address = 0;
    N64MemorySpace source_memory = N64MemorySpace::Rdram;
    std::vector<uint8_t> command_bytes;
    std::vector<uint8_t> source_pages;
    std::vector<N64MemoryPageSnapshot> referenced_pages;
};

// This is deliberately an RDP-level inspection result, not a reconstructed
// Godot scene. Fast3D display lists are produced upstream by the RSP; RDP is
// the lower-level, projection-dependent command stream that follows them.
struct N64RawRdpAnalysis {
    uint64_t sequence = 0;
    uint32_t command_count = 0;
    uint32_t triangle_count = 0;
    uint32_t texture_rectangle_count = 0;
    uint32_t set_texture_image_count = 0;
    uint32_t set_color_image_count = 0;
    uint32_t set_depth_image_count = 0;
    uint32_t image_reference_count = 0;
};
struct N64RawRdpCommandRecord {
    uint32_t byte_offset = 0;
    uint16_t byte_count = 0;
    uint8_t opcode = 0;
};

// One fully framed RDP command in callback order. Commands can cross raw-list
// callback boundaries, so the record keeps both range endpoints and the exact
// variable-length command bytes rather than pretending every range is an
// independently parseable display list.
struct N64ForensicRawRdpCommandRecord {
    uint32_t global_ordinal = 0;
    uint32_t first_range_index = 0;
    uint32_t last_range_index = 0;
    uint32_t first_range_byte_offset = 0;
    uint64_t first_range_sequence = 0;
    N64MemorySpace source_memory = N64MemorySpace::Rdram;
    uint32_t first_source_address = 0;
    uint8_t opcode = 0;
    std::vector<uint8_t> bytes;
};

// One command visited by the live Fast3D traversal. The vector position is
// the execution ordinal; addresses remain explicit because display lists may
// execute the same logical command more than once after segment rebases.
struct N64Fast3DCommandRecord {
    uint32_t depth = 0;
    uint32_t segmented_address = 0;
    uint32_t physical_rdram_address = 0;
    uint32_t word0 = 0;
    uint32_t word1 = 0;
};

enum class N64ForensicCorrelationPolicy : uint8_t {
    TaskToNextViCallback = 1,
    TaskToFirstRawRdpThenNextViCallback = 2,
};

// Immutable evidence captured from one selected graphics-task callback until
// a correlated VI callback. Geometry-bearing captures wait for at least one
// raw RDP submission and then seal at the following VI callback. task_rdram is
// explicitly the task-start snapshot; VI pixels are copied at scanout. They
// are correlated, not claimed to be the same instant in emulated time.
struct N64ForensicCaptureBundle {
    uint64_t capture_id = 0;
    N64ForensicCorrelationPolicy correlation_policy =
            N64ForensicCorrelationPolicy::TaskToNextViCallback;
    N64GraphicsTaskDescriptor task;
    std::vector<uint8_t> task_bytes;
    std::vector<uint8_t> task_rdram;
    std::vector<uint8_t> task_dmem;
    std::vector<uint8_t> task_imem;
    std::vector<N64Fast3DCommandRecord> fast3d_commands;
    std::vector<N64RawRdpCapture> raw_rdp_captures;
    N64ViFrame vi_frame;
    std::vector<uint8_t> normalized_scene_event;
};
class N64MemoryReader {
public:
    virtual ~N64MemoryReader() = default;

    virtual bool read(
            N64MemorySpace space,
            uint32_t address,
            size_t size,
            std::vector<uint8_t> &bytes,
            std::string &error) const = 0;
};

class N64GraphicsTaskTranslator {
public:
    virtual ~N64GraphicsTaskTranslator() = default;

    virtual bool translate(
            const N64GraphicsTaskDescriptor &task,
            const N64MemoryReader &memory,
            RenderSceneData &scene,
            std::string &error) = 0;
};

class N64RenderSink {
public:
    virtual ~N64RenderSink() = default;

    virtual bool submit_scene(
            const N64GraphicsTaskDescriptor &task,
            const RenderSceneData &scene,
            std::string &error) = 0;

    virtual bool submit_vi_frame(
            const N64ViFrame &frame,
            std::string &error) = 0;
};

// Routes emulator output into XR64-owned rendering paths. Supported graphics
// tasks become projection-independent scene data; unsupported tasks may use a
// completed VI frame as a compatibility fallback. No external graphics plugin
// belongs behind this interface.
class N64EmulatorRenderBridge {
public:
    N64EmulatorRenderBridge(
            N64GraphicsTaskTranslator &translator,
            N64RenderSink &sink);

    bool submit_graphics_task(
            const N64GraphicsTaskDescriptor &task,
            const N64MemoryReader &memory,
            std::string &error);

    bool submit_vi_frame(
            const N64ViFrame &frame,
            std::string &error);

private:
    N64GraphicsTaskTranslator &translator;
    N64RenderSink &sink;
};

bool validate_n64_vi_frame(
        const N64ViFrame &frame,
        std::string &error);
bool convert_n64_vi_frame_to_rgba8(
        const N64ViFrame &frame,
        std::vector<uint8_t> &rgba_pixels,
        std::string &error);
bool validate_n64_raw_rdp_capture(
        const N64RawRdpCapture &capture,
        std::string &error);
bool analyze_n64_raw_rdp_capture(
        const N64RawRdpCapture &capture,
        N64RawRdpAnalysis &analysis,
        std::string &error);
bool collect_n64_raw_rdp_resource_references(
        const N64RawRdpCapture &capture,
        std::vector<N64RdpResourceReference> &references,
        std::string &error);
bool collect_n64_raw_rdp_command_records(
        const N64RawRdpCapture &capture,
        std::vector<N64RawRdpCommandRecord> &records,
        std::string &error);
const char *n64_rdp_opcode_name(uint8_t opcode);
bool collect_n64_forensic_raw_rdp_command_records(
        const N64ForensicCaptureBundle &capture,
        std::vector<N64ForensicRawRdpCommandRecord> &records,
        std::string &error);

// A compact, checksummed, ROM-independent artifact for XR64's PC workbench.
// It contains only a bounded RDP range and memory snapshots selected by XR64;
// callers remain responsible for storing ROM-derived capture data privately.
bool write_n64_raw_rdp_capture_file(
        const std::string &path,
        const N64RawRdpCapture &capture,
        std::string &error);
bool read_n64_raw_rdp_capture_file(
        const std::string &path,
        N64RawRdpCapture &capture,
        std::string &error);

bool validate_n64_forensic_capture_bundle(
        const N64ForensicCaptureBundle &capture,
        std::string &error);
bool write_n64_forensic_capture_file(
        const std::string &path,
        const N64ForensicCaptureBundle &capture,
        std::string &error);
bool read_n64_forensic_capture_file(
        const std::string &path,
        N64ForensicCaptureBundle &capture,
        std::string &error);

} // namespace xr64
