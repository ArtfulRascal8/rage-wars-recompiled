#include "render_scene_replay.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

namespace xr64 {
namespace {

constexpr uint8_t event_magic[8] = {'X', 'R', '6', '4', 'S', 'C', 'N', 0};
constexpr uint32_t legacy_event_schema = 1;
constexpr uint32_t fast3d_event_schema = 2;
constexpr uint32_t draw_association_event_schema = 3;
constexpr uint32_t combiner_event_schema = 4;
constexpr uint32_t raster_event_schema = 5;
constexpr uint32_t raster_high_event_schema = 6;
constexpr uint32_t vertex_state_event_schema = 7;
constexpr uint32_t gbi_dialect_event_schema = 8;
constexpr uint32_t material_state_event_schema = 9;
constexpr uint32_t culling_state_event_schema = 10;
constexpr uint32_t event_schema = culling_state_event_schema;
constexpr uint32_t maximum_fast3d_captures = 100000;
constexpr uint32_t maximum_modelview_stack_depth = 32;
constexpr size_t header_size = 20;
constexpr size_t maximum_event_bytes = 16U * 1024U * 1024U;
constexpr uint32_t maximum_textures = 256;
constexpr uint32_t maximum_surfaces = 4096;
constexpr uint32_t maximum_vertices = 1U << 20U;
constexpr uint32_t maximum_indices = 3U << 20U;
constexpr uint32_t maximum_texture_dimension = 8192;

uint32_t crc32(const uint8_t *bytes, size_t size) {
    uint32_t value = 0xffffffffU;
    for (size_t index = 0; index < size; ++index) {
        value ^= bytes[index];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            value = (value >> 1U) ^
                    ((value & 1U) != 0 ? 0xedb88320U : 0U);
        }
    }
    return ~value;
}

bool append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
    if (bytes.size() > maximum_event_bytes - 4U) return false;
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
    return true;
}

bool append_float(std::vector<uint8_t> &bytes, float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    return append_u32(bytes, bits);
}

bool append_i32(std::vector<uint8_t> &bytes, int32_t value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "int32_t must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    return append_u32(bytes, bits);
}

bool append_i16(std::vector<uint8_t> &bytes, int16_t value) {
    uint16_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "int16_t must be 16-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    return append_u32(bytes, bits);
}


bool append_blob(
        std::vector<uint8_t> &bytes,
        const uint8_t *source,
        size_t size) {
    if (size > maximum_event_bytes - bytes.size()) return false;
    bytes.insert(bytes.end(), source, source + size);
    return true;
}

bool valid_texture(const ModelTexture &texture) {
    if (texture.width == 0 || texture.height == 0 ||
            texture.width > maximum_texture_dimension ||
            texture.height > maximum_texture_dimension) {
        return false;
    }
    const uint64_t expected = static_cast<uint64_t>(texture.width) *
            texture.height * 4U;
    return expected == texture.rgba_pixels.size();
}

bool finite_vertex(const ModelVertex &vertex) {
    const float values[] = {
            vertex.position_x, vertex.position_y, vertex.position_z,
            vertex.normal_x, vertex.normal_y, vertex.normal_z,
            vertex.uv_x, vertex.uv_y,
            vertex.color_r, vertex.color_g, vertex.color_b, vertex.color_a};
    for (float value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

class Reader {
public:
    Reader(const uint8_t *data_value, size_t size_value) :
            data(data_value), size(size_value) {}

    bool read_u32(uint32_t &value) {
        if (remaining() < 4U) return false;
        value = static_cast<uint32_t>(data[offset]) |
                (static_cast<uint32_t>(data[offset + 1U]) << 8U) |
                (static_cast<uint32_t>(data[offset + 2U]) << 16U) |
                (static_cast<uint32_t>(data[offset + 3U]) << 24U);
        offset += 4U;
        return true;
    }

    bool read_float(float &value) {
        uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value);
    }

    bool read_i32(int32_t &value) {
        uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool read_i16(int16_t &value) {
        uint32_t encoded = 0;
        if (!read_u32(encoded) ||
                encoded > std::numeric_limits<uint16_t>::max()) return false;
        const uint16_t bits = static_cast<uint16_t>(encoded);
        static_assert(sizeof(bits) == sizeof(value), "int16_t must be 16-bit");
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }


    bool read_blob(std::vector<uint8_t> &target, size_t count) {
        if (remaining() < count) return false;
        target.assign(data + offset, data + offset + count);
        offset += count;
        return true;
    }

    size_t remaining() const { return size - offset; }

private:
    const uint8_t *data = nullptr;
    size_t size = 0;
    size_t offset = 0;
};

bool append_vertex(std::vector<uint8_t> &bytes, const ModelVertex &vertex) {
    return append_float(bytes, vertex.position_x) &&
            append_float(bytes, vertex.position_y) &&
            append_float(bytes, vertex.position_z) &&
            append_float(bytes, vertex.normal_x) &&
            append_float(bytes, vertex.normal_y) &&
            append_float(bytes, vertex.normal_z) &&
            append_float(bytes, vertex.uv_x) &&
            append_float(bytes, vertex.uv_y) &&
            append_float(bytes, vertex.color_r) &&
            append_float(bytes, vertex.color_g) &&
            append_float(bytes, vertex.color_b) &&
            append_float(bytes, vertex.color_a) &&
            append_u32(bytes, vertex.fast3d_modelview_matrix_index) &&
            append_u32(bytes, vertex.fast3d_projection_matrix_index) &&
            append_u32(bytes, vertex.fast3d_viewport_index);
}

bool read_vertex(Reader &reader, ModelVertex &vertex, uint32_t schema) {
    if (!(reader.read_float(vertex.position_x) &&
            reader.read_float(vertex.position_y) &&
            reader.read_float(vertex.position_z) &&
            reader.read_float(vertex.normal_x) &&
            reader.read_float(vertex.normal_y) &&
            reader.read_float(vertex.normal_z) &&
            reader.read_float(vertex.uv_x) &&
            reader.read_float(vertex.uv_y) &&
            reader.read_float(vertex.color_r) &&
            reader.read_float(vertex.color_g) &&
            reader.read_float(vertex.color_b) &&
            reader.read_float(vertex.color_a))) {
        return false;
    }
    return schema < vertex_state_event_schema ||
            (reader.read_u32(vertex.fast3d_modelview_matrix_index) &&
                    reader.read_u32(vertex.fast3d_projection_matrix_index) &&
                    reader.read_u32(vertex.fast3d_viewport_index));
}
bool valid_fast3d_provenance(Fast3DStateProvenance provenance) {
    return provenance == Fast3DStateProvenance::Unavailable ||
            provenance == Fast3DStateProvenance::StaticSegmentData ||
            provenance == Fast3DStateProvenance::LiveRdramTaskSnapshot;
}

bool valid_fast3d_confidence(Fast3DStateConfidence confidence) {
    return confidence == Fast3DStateConfidence::Unavailable ||
            confidence == Fast3DStateConfidence::Captured;
}

bool decode_fast3d_provenance(
        uint32_t encoded,
        Fast3DStateProvenance &provenance) {
    if (encoded > static_cast<uint32_t>(
            Fast3DStateProvenance::LiveRdramTaskSnapshot)) {
        return false;
    }
    provenance = static_cast<Fast3DStateProvenance>(encoded);
    return valid_fast3d_provenance(provenance);
}

bool decode_fast3d_confidence(
        uint32_t encoded,
        Fast3DStateConfidence &confidence) {
    if (encoded > static_cast<uint32_t>(Fast3DStateConfidence::Captured)) {
        return false;
    }
    confidence = static_cast<Fast3DStateConfidence>(encoded);
    return valid_fast3d_confidence(confidence);
}

bool valid_capture_index(uint32_t index, size_t capture_count) {
    return index == FAST3D_CAPTURE_NO_INDEX || index < capture_count;
}

bool valid_fast3d_capture(const Fast3DSemanticCapture &fast3d) {
    if (!valid_fast3d_provenance(fast3d.provenance) ||
            !valid_fast3d_confidence(fast3d.segment_confidence) ||
            !valid_fast3d_confidence(fast3d.matrix_confidence) ||
            !valid_fast3d_confidence(fast3d.viewport_confidence) ||
            !valid_fast3d_confidence(fast3d.draw_state_confidence) ||
            !valid_fast3d_confidence(fast3d.camera_confidence) ||
            fast3d.matrices.size() > maximum_fast3d_captures ||
            fast3d.viewports.size() > maximum_fast3d_captures ||
            fast3d.draws.size() > maximum_fast3d_captures) {
        return false;
    }
    if (static_cast<uint32_t>(fast3d.microcode_family) >
                    static_cast<uint32_t>(N64MicrocodeFamily::S2DEX) ||
            fast3d.no_op_command_count > fast3d.command_count ||
            fast3d.passthrough_command_count > fast3d.command_count ||
            fast3d.no_op_command_count >
                    fast3d.command_count - fast3d.passthrough_command_count ||
            fast3d.passthrough_opcodes.size() > 256U ||
            (fast3d.passthrough_command_count == 0U) !=
                    fast3d.passthrough_opcodes.empty()) {
        return false;
    }
    std::array<bool, 256> seen_passthrough_opcodes{};
    for (uint8_t opcode : fast3d.passthrough_opcodes) {
        if (seen_passthrough_opcodes[opcode]) return false;
        seen_passthrough_opcodes[opcode] = true;
    }
    if (fast3d.provenance == Fast3DStateProvenance::Unavailable &&
            (fast3d.segment_state_available || !fast3d.matrices.empty() ||
                    !fast3d.viewports.empty() || !fast3d.draws.empty())) {
        return false;
    }
    if (fast3d.segment_state_available !=
                    (fast3d.segment_confidence ==
                            Fast3DStateConfidence::Captured) ||
            (!fast3d.matrices.empty()) !=
                    (fast3d.matrix_confidence ==
                            Fast3DStateConfidence::Captured) ||
            (!fast3d.viewports.empty()) !=
                    (fast3d.viewport_confidence ==
                            Fast3DStateConfidence::Captured) ||
            (!fast3d.draws.empty()) !=
                    (fast3d.draw_state_confidence ==
                            Fast3DStateConfidence::Captured) ||
            (fast3d.camera_confidence == Fast3DStateConfidence::Captured &&
                    (fast3d.matrix_confidence !=
                                    Fast3DStateConfidence::Captured ||
                            fast3d.viewport_confidence !=
                                    Fast3DStateConfidence::Captured ||
                            fast3d.draw_state_confidence !=
                                    Fast3DStateConfidence::Captured))) {
        return false;
    }

    for (size_t index = 0; index < fast3d.segment_bases.size(); ++index) {
        if (fast3d.segment_loaded[index] > 1U ||
                (!fast3d.segment_state_available &&
                        (fast3d.segment_bases[index] != 0U ||
                                fast3d.segment_loaded[index] != 0U))) {
            return false;
        }
    }
    for (size_t index = 0; index < fast3d.matrices.size(); ++index) {
        const Fast3DMatrixCapture &matrix = fast3d.matrices[index];
        if (matrix.modelview_stack_depth > maximum_modelview_stack_depth ||
                !valid_capture_index(
                        matrix.previous_matrix_index,
                        fast3d.matrices.size()) ||
                (matrix.previous_matrix_index != FAST3D_CAPTURE_NO_INDEX &&
                        (matrix.previous_matrix_index >= index ||
                                fast3d.matrices[matrix.previous_matrix_index]
                                        .projection != matrix.projection))) {
            return false;
        }
    }
    for (const Fast3DDrawStateCapture &draw : fast3d.draws) {
        if (draw.modelview_stack_depth > maximum_modelview_stack_depth ||
                !valid_capture_index(
                        draw.modelview_matrix_index,
                        fast3d.matrices.size()) ||
                !valid_capture_index(
                        draw.projection_matrix_index,
                        fast3d.matrices.size()) ||
                !valid_capture_index(
                        draw.viewport_index,
                        fast3d.viewports.size()) ||
                (draw.modelview_matrix_index != FAST3D_CAPTURE_NO_INDEX &&
                        fast3d.matrices[draw.modelview_matrix_index].projection) ||
                (draw.projection_matrix_index != FAST3D_CAPTURE_NO_INDEX &&
                        !fast3d.matrices[draw.projection_matrix_index].projection)) {
            return false;
        }
    }
    return true;
}

bool append_fast3d_capture(
        std::vector<uint8_t> &bytes,
        const Fast3DSemanticCapture &fast3d) {
    if (!valid_fast3d_capture(fast3d) ||
            !append_u32(bytes, static_cast<uint32_t>(fast3d.provenance)) ||
            !append_u32(bytes,
                    static_cast<uint32_t>(fast3d.segment_confidence)) ||
            !append_u32(bytes,
                    static_cast<uint32_t>(fast3d.matrix_confidence)) ||
            !append_u32(bytes,
                    static_cast<uint32_t>(fast3d.viewport_confidence)) ||
            !append_u32(bytes,
                    static_cast<uint32_t>(fast3d.draw_state_confidence)) ||
            !append_u32(bytes,
                    static_cast<uint32_t>(fast3d.camera_confidence)) ||
            !append_u32(bytes, fast3d.segment_state_available ? 1U : 0U)) {
        return false;
    }
    for (size_t index = 0; index < fast3d.segment_bases.size(); ++index) {
        if (!append_u32(bytes, fast3d.segment_bases[index]) ||
                !append_u32(bytes, fast3d.segment_loaded[index])) {
            return false;
        }
    }
    if (!append_u32(bytes, static_cast<uint32_t>(fast3d.matrices.size()))) {
        return false;
    }
    for (const Fast3DMatrixCapture &matrix : fast3d.matrices) {
        const uint32_t flags = (matrix.projection ? 1U : 0U) |
                (matrix.load ? 2U : 0U) | (matrix.push ? 4U : 0U);
        if (!append_u32(bytes, matrix.command_address) ||
                !append_u32(bytes, matrix.matrix_address) ||
                !append_u32(bytes, matrix.parameters) ||
                !append_u32(bytes, flags) ||
                !append_u32(bytes, matrix.previous_matrix_index) ||
                !append_u32(bytes, matrix.modelview_stack_depth)) {
            return false;
        }
        for (int32_t value : matrix.fixed_16_16) {
            if (!append_i32(bytes, value)) return false;
        }
    }
    if (!append_u32(bytes, static_cast<uint32_t>(fast3d.viewports.size()))) {
        return false;
    }
    for (const Fast3DViewportCapture &viewport : fast3d.viewports) {
        if (!append_u32(bytes, viewport.command_address) ||
                !append_u32(bytes, viewport.viewport_address)) {
            return false;
        }
        for (int16_t value : viewport.scale) {
            if (!append_i16(bytes, value)) return false;
        }
        for (int16_t value : viewport.translate) {
            if (!append_i16(bytes, value)) return false;
        }
    }
    if (!append_u32(bytes, static_cast<uint32_t>(fast3d.draws.size()))) {
        return false;
    }
    for (const Fast3DDrawStateCapture &draw : fast3d.draws) {
        if (!append_u32(bytes, draw.command_address) ||
                !append_u32(bytes, draw.modelview_matrix_index) ||
                !append_u32(bytes, draw.projection_matrix_index) ||
                !append_u32(bytes, draw.viewport_index) ||
                !append_u32(bytes, draw.modelview_stack_depth)) {
            return false;
        }
    }
    if (!append_u32(bytes, static_cast<uint32_t>(fast3d.microcode_family)) ||
            !append_u32(bytes, fast3d.command_count) ||
            !append_u32(bytes, fast3d.no_op_command_count) ||
            !append_u32(bytes, fast3d.passthrough_command_count) ||
            !append_u32(bytes,
                    static_cast<uint32_t>(fast3d.passthrough_opcodes.size()))) {
        return false;
    }
    for (uint8_t opcode : fast3d.passthrough_opcodes) {
        if (!append_u32(bytes, opcode)) return false;
    }
    return true;
}

bool read_fast3d_capture(
        Reader &reader,
        Fast3DSemanticCapture &fast3d,
        uint32_t schema) {
    uint32_t provenance = 0;
    uint32_t segment_confidence = 0;
    uint32_t matrix_confidence = 0;
    uint32_t viewport_confidence = 0;
    uint32_t draw_state_confidence = 0;
    uint32_t camera_confidence = 0;
    uint32_t segment_state_available = 0;
    if (!reader.read_u32(provenance) ||
            !reader.read_u32(segment_confidence) ||
            !reader.read_u32(matrix_confidence) ||
            !reader.read_u32(viewport_confidence) ||
            !reader.read_u32(draw_state_confidence) ||
            !reader.read_u32(camera_confidence) ||
            !reader.read_u32(segment_state_available) ||
            segment_state_available > 1U ||
            !decode_fast3d_provenance(provenance, fast3d.provenance) ||
            !decode_fast3d_confidence(
                    segment_confidence, fast3d.segment_confidence) ||
            !decode_fast3d_confidence(
                    matrix_confidence, fast3d.matrix_confidence) ||
            !decode_fast3d_confidence(
                    viewport_confidence, fast3d.viewport_confidence) ||
            !decode_fast3d_confidence(
                    draw_state_confidence, fast3d.draw_state_confidence) ||
            !decode_fast3d_confidence(
                    camera_confidence, fast3d.camera_confidence)) {
        return false;
    }
    fast3d.segment_state_available = segment_state_available != 0U;
    for (size_t index = 0; index < fast3d.segment_bases.size(); ++index) {
        uint32_t loaded = 0;
        if (!reader.read_u32(fast3d.segment_bases[index]) ||
                !reader.read_u32(loaded) || loaded > 1U) {
            return false;
        }
        fast3d.segment_loaded[index] = static_cast<uint8_t>(loaded);
    }

    uint32_t matrix_count = 0;
    if (!reader.read_u32(matrix_count) ||
            matrix_count > maximum_fast3d_captures) {
        return false;
    }
    fast3d.matrices.resize(matrix_count);
    for (Fast3DMatrixCapture &matrix : fast3d.matrices) {
        uint32_t parameters = 0;
        uint32_t flags = 0;
        if (!reader.read_u32(matrix.command_address) ||
                !reader.read_u32(matrix.matrix_address) ||
                !reader.read_u32(parameters) ||
                parameters > std::numeric_limits<uint8_t>::max() ||
                !reader.read_u32(flags) || flags > 7U ||
                !reader.read_u32(matrix.previous_matrix_index) ||
                !reader.read_u32(matrix.modelview_stack_depth)) {
            return false;
        }
        matrix.parameters = static_cast<uint8_t>(parameters);
        matrix.projection = (flags & 1U) != 0;
        matrix.load = (flags & 2U) != 0;
        matrix.push = (flags & 4U) != 0;
        for (int32_t &value : matrix.fixed_16_16) {
            if (!reader.read_i32(value)) return false;
        }
    }

    uint32_t viewport_count = 0;
    if (!reader.read_u32(viewport_count) ||
            viewport_count > maximum_fast3d_captures) {
        return false;
    }
    fast3d.viewports.resize(viewport_count);
    for (Fast3DViewportCapture &viewport : fast3d.viewports) {
        if (!reader.read_u32(viewport.command_address) ||
                !reader.read_u32(viewport.viewport_address)) {
            return false;
        }
        for (int16_t &value : viewport.scale) {
            if (!reader.read_i16(value)) return false;
        }
        for (int16_t &value : viewport.translate) {
            if (!reader.read_i16(value)) return false;
        }
    }

    uint32_t draw_count = 0;
    if (!reader.read_u32(draw_count) || draw_count > maximum_fast3d_captures) {
        return false;
    }
    fast3d.draws.resize(draw_count);
    for (Fast3DDrawStateCapture &draw : fast3d.draws) {
        if (!reader.read_u32(draw.command_address) ||
                !reader.read_u32(draw.modelview_matrix_index) ||
                !reader.read_u32(draw.projection_matrix_index) ||
                !reader.read_u32(draw.viewport_index) ||
                !reader.read_u32(draw.modelview_stack_depth)) {
            return false;
        }
    }
    if (schema >= gbi_dialect_event_schema) {
        uint32_t family = 0;
        uint32_t passthrough_opcode_count = 0;
        if (!reader.read_u32(family) ||
                family > static_cast<uint32_t>(N64MicrocodeFamily::S2DEX) ||
                !reader.read_u32(fast3d.command_count) ||
                !reader.read_u32(fast3d.no_op_command_count) ||
                !reader.read_u32(fast3d.passthrough_command_count) ||
                !reader.read_u32(passthrough_opcode_count) ||
                passthrough_opcode_count > 256U) {
            return false;
        }
        fast3d.microcode_family =
                static_cast<N64MicrocodeFamily>(family);
        fast3d.passthrough_opcodes.resize(passthrough_opcode_count);
        for (uint8_t &opcode : fast3d.passthrough_opcodes) {
            uint32_t encoded_opcode = 0;
            if (!reader.read_u32(encoded_opcode) || encoded_opcode > 0xffU) {
                return false;
            }
            opcode = static_cast<uint8_t>(encoded_opcode);
        }
    }
    return valid_fast3d_capture(fast3d);
}

} // namespace

bool encode_render_scene_event(
        const RenderSceneData &scene,
        std::vector<uint8_t> &bytes,
        std::string &error) {
    bytes.clear();
    if (scene.textures.size() > maximum_textures ||
            scene.surfaces.size() > maximum_surfaces) {
        error = "Normalized scene exceeds XR64 resource-count limits.";
        return false;
    }
    if (!scene.textures.empty() &&
            scene.flat_reference_texture_index >= scene.textures.size()) {
        error = "Normalized scene flat texture index is out of range.";
        return false;
    }

    std::vector<uint8_t> payload;
    if (!append_u32(payload, scene.flat_reference_texture_index) ||
            !append_u32(payload, static_cast<uint32_t>(scene.textures.size()))) {
        error = "Normalized scene exceeds XR64 event size limit.";
        return false;
    }
    for (const ModelTexture &texture : scene.textures) {
        if (!valid_texture(texture)) {
            error = "Normalized scene contains an invalid RGBA8 texture.";
            return false;
        }
        if (!append_u32(payload, texture.width) ||
                !append_u32(payload, texture.height) ||
                !append_u32(payload,
                        static_cast<uint32_t>(texture.rgba_pixels.size())) ||
                !append_blob(payload, texture.rgba_pixels.data(),
                        texture.rgba_pixels.size())) {
            error = "Normalized scene exceeds XR64 event size limit.";
            return false;
        }
    }
    if (!append_u32(payload, static_cast<uint32_t>(scene.surfaces.size()))) {
        error = "Normalized scene exceeds XR64 event size limit.";
        return false;
    }
    for (const ModelSurface &surface : scene.surfaces) {
        if (surface.vertices.size() > maximum_vertices ||
                surface.indices.size() > maximum_indices ||
                surface.indices.size() % 3U != 0U ||
                (!surface.triangle_draw_indices.empty() &&
                        surface.triangle_draw_indices.size() !=
                                surface.indices.size() / 3U) ||
                (!scene.textures.empty() &&
                        (surface.texture_index >= scene.textures.size() ||
                                (surface.secondary_texture_index != FAST3D_CAPTURE_NO_INDEX &&
                                        surface.secondary_texture_index >= scene.textures.size())))) {
            error = "Normalized scene surface exceeds XR64 limits.";
            return false;
        }
        for (uint32_t draw_index : surface.triangle_draw_indices) {
            if (draw_index >= scene.fast3d.draws.size()) {
                error = "Normalized scene contains an invalid draw-state reference.";
                return false;
            }
        }
        const uint32_t flags = (surface.alpha_blend ? 1U : 0U) |
                (surface.alpha_cutout ? 2U : 0U) |
                (surface.clamp_s ? 4U : 0U) |
                (surface.clamp_t ? 8U : 0U) |
                (surface.cull_front ? 16U : 0U) |
                (surface.cull_back ? 32U : 0U);
        const uint32_t combiner_flags =
                (surface.rdp_combiner.captured ? 1U : 0U) |
                (surface.rdp_combiner.primitive_color_captured ? 2U : 0U) |
                (surface.rdp_combiner.environment_color_captured ? 4U : 0U) |
                (static_cast<uint32_t>(surface.rdp_combiner.mode) << 8U);
        if (static_cast<uint32_t>(surface.rdp_combiner.mode) >
                        static_cast<uint32_t>(RdpCombinerMode::PrimitiveEnvironmentShade)) {
            error = "Normalized scene contains an invalid RDP combiner mode.";
            return false;
        }
        const uint32_t raster_flags =
                (surface.rdp_raster.other_mode_low_captured ? 1U : 0U) |
                (surface.rdp_raster.z_buffer_enabled ? 2U : 0U) |
                (surface.rdp_raster.depth_compare ? 4U : 0U) |
                (surface.rdp_raster.depth_write ? 8U : 0U) |
                (surface.rdp_raster.alpha_compare ? 16U : 0U) |
                (surface.rdp_raster.force_blend ? 32U : 0U) |
                (surface.rdp_raster.coverage_times_alpha ? 64U : 0U) |
                (surface.rdp_raster.alpha_coverage_select ? 128U : 0U) |
                (static_cast<uint32_t>(surface.rdp_raster.coverage_destination) << 8U);
        const uint32_t raster_high_flags =
                (surface.rdp_raster.other_mode_high_captured ? 1U : 0U) |
                (static_cast<uint32_t>(surface.rdp_raster.cycle_type) << 8U);
        if (surface.rdp_raster.coverage_destination > 3U ||
                surface.rdp_raster.cycle_type > 3U) {
            error = "Normalized scene contains an invalid RDP coverage mode.";
            return false;
        }
        if (!append_u32(payload, surface.texture_index) ||
                !append_u32(payload, surface.render_layer) ||
                !append_u32(payload, flags) ||
                !append_u32(payload, combiner_flags) ||
                !append_u32(payload, surface.rdp_combiner.word0) ||
                !append_u32(payload, surface.rdp_combiner.word1) ||
                !append_u32(payload, surface.secondary_texture_index) ||
                !append_u32(payload, surface.rdp_combiner.primitive_color) ||
                !append_u32(payload, surface.rdp_combiner.environment_color) ||
                !append_u32(payload, raster_flags) ||
                !append_u32(payload, surface.rdp_raster.other_mode_low) ||
                !append_u32(payload, raster_high_flags) ||
                !append_u32(payload, surface.rdp_raster.other_mode_high) ||
                !append_u32(payload,
                        static_cast<uint32_t>(surface.vertices.size())) ||
                !append_u32(payload,
                        static_cast<uint32_t>(surface.indices.size()))) {
            error = "Normalized scene exceeds XR64 event size limit.";
            return false;
        }
        for (const ModelVertex &vertex : surface.vertices) {
            if (!finite_vertex(vertex) || !append_vertex(payload, vertex)) {
                error = "Normalized scene contains invalid or excessive vertex data.";
                return false;
            }
        }
        for (uint32_t index : surface.indices) {
            if (index >= surface.vertices.size() || !append_u32(payload, index)) {
                error = "Normalized scene contains an invalid triangle index.";
                return false;
            }
        }
        if (!append_u32(payload,
                    static_cast<uint32_t>(surface.triangle_draw_indices.size()))) {
            error = "Normalized scene exceeds XR64 event size limit.";
            return false;
        }
        for (uint32_t draw_index : surface.triangle_draw_indices) {
            if (!append_u32(payload, draw_index)) {
                error = "Normalized scene exceeds XR64 event size limit.";
                return false;
            }
        }
    }
    if (!append_fast3d_capture(payload, scene.fast3d)) {
        error = "Normalized scene contains invalid or excessive Fast3D state.";
        return false;
    }

    if (payload.size() > maximum_event_bytes - header_size) {
        error = "Normalized scene exceeds XR64 event size limit.";
        return false;
    }
    bytes.insert(bytes.end(), event_magic, event_magic + sizeof(event_magic));
    append_u32(bytes, event_schema);
    append_u32(bytes, static_cast<uint32_t>(payload.size()));
    append_u32(bytes, crc32(payload.data(), payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    error.clear();
    return true;
}

bool decode_render_scene_event(
        const uint8_t *bytes,
        size_t byte_count,
        RenderSceneData &scene,
        std::string &error) {
    scene = {};
    if (bytes == nullptr || byte_count < header_size ||
            byte_count > maximum_event_bytes ||
            std::memcmp(bytes, event_magic, sizeof(event_magic)) != 0) {
        error = "Normalized scene event has an invalid header.";
        return false;
    }
    Reader header(bytes + sizeof(event_magic), header_size - sizeof(event_magic));
    uint32_t schema = 0;
    uint32_t payload_size = 0;
    uint32_t expected_crc = 0;
    if (!header.read_u32(schema) || !header.read_u32(payload_size) ||
            !header.read_u32(expected_crc) ||
            (schema != legacy_event_schema && schema != fast3d_event_schema &&
                    schema != draw_association_event_schema &&
                    schema != combiner_event_schema &&
                    schema != raster_event_schema &&
                    schema != raster_high_event_schema &&
                    schema != vertex_state_event_schema &&
                    schema != gbi_dialect_event_schema &&
                    schema != event_schema) ||
            payload_size != byte_count - header_size) {
        error = "Normalized scene event schema or size is invalid.";
        return false;
    }
    const uint8_t *payload = bytes + header_size;
    if (crc32(payload, payload_size) != expected_crc) {
        error = "Normalized scene event checksum failed.";
        return false;
    }

    Reader reader(payload, payload_size);
    uint32_t texture_count = 0;
    if (!reader.read_u32(scene.flat_reference_texture_index) ||
            !reader.read_u32(texture_count) ||
            texture_count > maximum_textures) {
        error = "Normalized scene event texture header is invalid.";
        return false;
    }
    scene.textures.resize(texture_count);
    for (ModelTexture &texture : scene.textures) {
        uint32_t pixel_count = 0;
        if (!reader.read_u32(texture.width) ||
                !reader.read_u32(texture.height) ||
                !reader.read_u32(pixel_count) ||
                pixel_count > reader.remaining() ||
                !reader.read_blob(texture.rgba_pixels, pixel_count) ||
                !valid_texture(texture)) {
            error = "Normalized scene event contains an invalid texture.";
            return false;
        }
    }

    uint32_t surface_count = 0;
    if (!reader.read_u32(surface_count) || surface_count > maximum_surfaces) {
        error = "Normalized scene event surface header is invalid.";
        return false;
    }
    scene.surfaces.resize(surface_count);
    for (ModelSurface &surface : scene.surfaces) {
        uint32_t flags = 0;
        uint32_t combiner_flags = 0;
        uint32_t raster_flags = 0;
        uint32_t raster_high_flags = 0;
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        if (!reader.read_u32(surface.texture_index) ||
                !reader.read_u32(surface.render_layer) ||
                !reader.read_u32(flags) ||
                flags > (schema >= culling_state_event_schema ? 63U : 15U) ||
                (schema >= combiner_event_schema &&
                        (!reader.read_u32(combiner_flags) ||
                                (combiner_flags & (schema >= material_state_event_schema ?
                                        0xffff00f8U : 0xffff00feU)) != 0U ||
                                (combiner_flags >> 8U) > static_cast<uint32_t>(
                                        schema >= material_state_event_schema ?
                                                RdpCombinerMode::PrimitiveEnvironmentShade :
                                                RdpCombinerMode::Unsupported) ||
                                !reader.read_u32(surface.rdp_combiner.word0) ||
                                !reader.read_u32(surface.rdp_combiner.word1))) ||
                (schema >= material_state_event_schema &&
                        (!reader.read_u32(surface.secondary_texture_index) ||
                                !reader.read_u32(surface.rdp_combiner.primitive_color) ||
                                !reader.read_u32(surface.rdp_combiner.environment_color))) ||
                (schema >= raster_event_schema &&
                        (!reader.read_u32(raster_flags) ||
                                (raster_flags & 0xfffffc00U) != 0U ||
                                ((raster_flags >> 8U) & 0x03U) > 3U ||
                                !reader.read_u32(surface.rdp_raster.other_mode_low))) ||
                (schema >= raster_high_event_schema &&
                        (!reader.read_u32(raster_high_flags) ||
                                (raster_high_flags & 0xfffffcfeU) != 0U ||
                                ((raster_high_flags >> 8U) & 0x03U) > 3U ||
                                !reader.read_u32(surface.rdp_raster.other_mode_high))) ||
                !reader.read_u32(vertex_count) ||
                !reader.read_u32(index_count) ||
                vertex_count > maximum_vertices ||
                index_count > maximum_indices ||
                (!scene.textures.empty() &&
                        (surface.texture_index >= scene.textures.size() ||
                                (surface.secondary_texture_index != FAST3D_CAPTURE_NO_INDEX &&
                                        surface.secondary_texture_index >= scene.textures.size())))) {
            error = "Normalized scene event contains an invalid surface.";
            return false;
        }
        surface.alpha_blend = (flags & 1U) != 0;
        surface.alpha_cutout = (flags & 2U) != 0;
        surface.cull_front = (flags & 16U) != 0;
        surface.cull_back = (flags & 32U) != 0;
        if (schema >= combiner_event_schema) {
            surface.rdp_combiner.captured = (combiner_flags & 1U) != 0U;
            surface.rdp_combiner.primitive_color_captured =
                    (combiner_flags & 2U) != 0U;
            surface.rdp_combiner.environment_color_captured =
                    (combiner_flags & 4U) != 0U;
            surface.rdp_combiner.mode = static_cast<RdpCombinerMode>(
                    combiner_flags >> 8U);
        }
        if (schema >= raster_event_schema) {
            surface.rdp_raster.other_mode_low_captured =
                    (raster_flags & 1U) != 0U;
            surface.rdp_raster.z_buffer_enabled =
                    (raster_flags & 2U) != 0U;
            surface.rdp_raster.depth_compare =
                    (raster_flags & 4U) != 0U;
            surface.rdp_raster.depth_write =
                    (raster_flags & 8U) != 0U;
            surface.rdp_raster.alpha_compare =
                    (raster_flags & 16U) != 0U;
            surface.rdp_raster.force_blend =
                    (raster_flags & 32U) != 0U;
            surface.rdp_raster.coverage_times_alpha =
                    (raster_flags & 64U) != 0U;
            surface.rdp_raster.alpha_coverage_select =
                    (raster_flags & 128U) != 0U;
            surface.rdp_raster.coverage_destination = static_cast<uint8_t>(
                    (raster_flags >> 8U) & 0x03U);
        }
        if (schema >= raster_high_event_schema) {
            surface.rdp_raster.other_mode_high_captured =
                    (raster_high_flags & 1U) != 0U;
            surface.rdp_raster.cycle_type = static_cast<uint8_t>(
                    (raster_high_flags >> 8U) & 0x03U);
        }
        surface.clamp_s = (flags & 4U) != 0;
        surface.clamp_t = (flags & 8U) != 0;
        surface.vertices.resize(vertex_count);
        for (ModelVertex &vertex : surface.vertices) {
            if (!read_vertex(reader, vertex, schema)) {
                error = "Normalized scene event contains invalid vertex data.";
                return false;
            }
        }
        surface.indices.resize(index_count);
        for (uint32_t &index : surface.indices) {
            if (!reader.read_u32(index) || index >= vertex_count) {
                error = "Normalized scene event contains an invalid triangle index.";
                return false;
            }
        }
        if (schema >= draw_association_event_schema) {
            uint32_t triangle_draw_count = 0;
            if (index_count % 3U != 0U ||
                    !reader.read_u32(triangle_draw_count) ||
                    triangle_draw_count > index_count / 3U ||
                    (triangle_draw_count != 0U &&
                            triangle_draw_count != index_count / 3U)) {
                error = "Normalized scene event contains invalid draw-state associations.";
                return false;
            }
            surface.triangle_draw_indices.resize(triangle_draw_count);
            for (uint32_t &draw_index : surface.triangle_draw_indices) {
                if (!reader.read_u32(draw_index)) {
                    error = "Normalized scene event contains invalid draw-state associations.";
                    return false;
                }
            }
        }
    }
    if (schema >= fast3d_event_schema &&
            !read_fast3d_capture(reader, scene.fast3d, schema)) {
        error = "Normalized scene event contains invalid Fast3D state.";
        scene = {};
        return false;
    }
    for (const ModelSurface &surface : scene.surfaces) {
        for (uint32_t draw_index : surface.triangle_draw_indices) {
            if (draw_index >= scene.fast3d.draws.size()) {
                error = "Normalized scene event contains an invalid draw-state reference.";
                scene = {};
                return false;
            }
        }
    }
    if (reader.remaining() != 0 ||
            (!scene.textures.empty() &&
                    scene.flat_reference_texture_index >= scene.textures.size())) {
        error = "Normalized scene event has trailing or invalid data.";
        scene = {};
        return false;
    }
    error.clear();
    return true;
}

bool project_render_scene_fast3d(
        const RenderSceneData &scene,
        RenderSceneData &projected_scene,
        Fast3DProjectionSummary &summary,
        std::string &error) {
    projected_scene = {};
    summary = {};
    summary.microcode_family = scene.fast3d.microcode_family;
    summary.gbi_command_count = scene.fast3d.command_count;
    summary.gbi_no_op_command_count = scene.fast3d.no_op_command_count;
    summary.gbi_passthrough_command_count =
            scene.fast3d.passthrough_command_count;
    summary.gbi_passthrough_opcodes = scene.fast3d.passthrough_opcodes;
    summary.camera_interpretation_verified =
            scene.fast3d.camera_confidence == Fast3DStateConfidence::Captured;
    summary.draw_count = static_cast<uint32_t>(scene.fast3d.draws.size());

    const auto fail = [&](const char *reason) {
        summary.reason = reason;
        error = reason;
        projected_scene = {};
        return false;
    };
    if (scene.surfaces.empty()) {
        return fail("no_renderable_scene_geometry");
    }
    if (scene.fast3d.matrix_confidence != Fast3DStateConfidence::Captured ||
            scene.fast3d.matrices.empty()) {
        return fail("missing_captured_fast3d_matrices");
    }
    if (scene.fast3d.viewport_confidence != Fast3DStateConfidence::Captured ||
            scene.fast3d.viewports.empty()) {
        return fail("missing_captured_fast3d_viewport");
    }
    if (scene.fast3d.draw_state_confidence !=
                    Fast3DStateConfidence::Captured ||
            scene.fast3d.draws.empty()) {
        return fail("missing_captured_fast3d_draw_state");
    }

    bool any_associations = false;
    bool missing_associations = false;
    for (const ModelSurface &surface : scene.surfaces) {
        if (surface.indices.size() % 3U != 0U) {
            return fail("scene_topology_is_not_triangles");
        }
        const size_t triangle_count = surface.indices.size() / 3U;
        if (surface.triangle_draw_indices.empty()) {
            missing_associations = missing_associations || triangle_count != 0U;
            continue;
        }
        if (surface.triangle_draw_indices.size() != triangle_count) {
            return fail("invalid_triangle_draw_association_count");
        }
        any_associations = true;
        for (uint32_t draw_index : surface.triangle_draw_indices) {
            if (draw_index >= scene.fast3d.draws.size()) {
                return fail("invalid_triangle_draw_association");
            }
        }
    }
    if (any_associations && missing_associations) {
        return fail("incomplete_triangle_draw_association");
    }
    summary.uses_per_draw_state = any_associations;
    summary.uses_latest_draw_state = !any_associations;

    struct ProjectionState {
        bool ready = false;
        std::array<double, 16> modelview{};
        std::array<double, 16> projection{};
        const Fast3DViewportCapture *viewport = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    std::map<std::array<uint32_t, 3>, ProjectionState> states;
    const auto capture_matrix = [](const Fast3DMatrixCapture &capture) {
        std::array<double, 16> matrix{};
        for (size_t index = 0; index < matrix.size(); ++index) {
            matrix[index] =
                    static_cast<double>(capture.fixed_16_16[index]) / 65536.0;
        }
        return matrix;
    };
    const auto multiply_matrices = [](
            const std::array<double, 16> &lhs,
            const std::array<double, 16> &rhs) {
        std::array<double, 16> result{};
        for (size_t row = 0; row < 4U; ++row) {
            for (size_t column = 0; column < 4U; ++column) {
                for (size_t inner = 0; inner < 4U; ++inner) {
                    result[row * 4U + column] +=
                            lhs[row * 4U + inner] *
                            rhs[inner * 4U + column];
                }
            }
        }
        return result;
    };
    const auto resolve_matrix = [&](
            uint32_t matrix_index,
            std::array<double, 16> &resolved) {
        bool first = true;
        while (matrix_index != FAST3D_CAPTURE_NO_INDEX) {
            if (matrix_index >= scene.fast3d.matrices.size()) return false;
            const Fast3DMatrixCapture &capture =
                    scene.fast3d.matrices[matrix_index];
            const std::array<double, 16> local = capture_matrix(capture);
            resolved = first ? local : multiply_matrices(resolved, local);
            first = false;
            if (capture.load) break;
            matrix_index = capture.previous_matrix_index;
        }
        return !first;
    };
    const auto resolve_state_indices = [&](
            uint32_t modelview_matrix_index,
            uint32_t projection_matrix_index,
            uint32_t viewport_index) -> ProjectionState * {
        const std::array<uint32_t, 3> key = {modelview_matrix_index,
                projection_matrix_index, viewport_index};
        ProjectionState &state = states[key];
        if (state.ready) return &state;
        if (modelview_matrix_index == FAST3D_CAPTURE_NO_INDEX ||
                modelview_matrix_index >= scene.fast3d.matrices.size() ||
                projection_matrix_index == FAST3D_CAPTURE_NO_INDEX ||
                projection_matrix_index >= scene.fast3d.matrices.size() ||
                viewport_index == FAST3D_CAPTURE_NO_INDEX ||
                viewport_index >= scene.fast3d.viewports.size()) {
            return nullptr;
        }
        const Fast3DMatrixCapture &modelview =
                scene.fast3d.matrices[modelview_matrix_index];
        const Fast3DMatrixCapture &projection =
                scene.fast3d.matrices[projection_matrix_index];
        if (modelview.projection || !projection.projection) return nullptr;
        const Fast3DViewportCapture &viewport =
                scene.fast3d.viewports[viewport_index];
        // N64 Vp x/y values carry two fractional bits. The full viewport span
        // is 2 * scale / 4, not 2 * scale pixels.
        const int32_t width = std::abs(static_cast<int32_t>(viewport.scale[0])) / 2;
        const int32_t height = std::abs(static_cast<int32_t>(viewport.scale[1])) / 2;
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
            return nullptr;
        }
        if (!resolve_matrix(modelview_matrix_index, state.modelview) ||
                !resolve_matrix(projection_matrix_index, state.projection)) {
            return nullptr;
        }
        state.viewport = &viewport;
        state.width = static_cast<uint32_t>(width);
        state.height = static_cast<uint32_t>(height);
        state.ready = true;
        return &state;
    };
    const auto resolve_state = [&](uint32_t draw_index) -> ProjectionState * {
        if (draw_index >= scene.fast3d.draws.size()) return nullptr;
        const Fast3DDrawStateCapture &draw = scene.fast3d.draws[draw_index];
        return resolve_state_indices(draw.modelview_matrix_index,
                draw.projection_matrix_index, draw.viewport_index);
    };
    const auto multiply = [](
            const std::array<double, 4> &vector,
            const std::array<double, 16> &matrix) {
        std::array<double, 4> result{};
        for (size_t column = 0; column < 4U; ++column) {
            for (size_t row = 0; row < 4U; ++row) {
                result[column] += vector[row] * matrix[row * 4U + column];
            }
        }
        return result;
    };

    projected_scene = scene;
    struct ClipVertex {
        ModelVertex vertex;
        std::array<double, 4> position{};
    };
    const auto interpolate_clip_vertex = [](
            const ClipVertex &from, const ClipVertex &to, double t) {
        ClipVertex result;
        const double clamped_t = std::clamp(t, 0.0, 1.0);
        const auto lerp = [clamped_t](double a, double b) {
            return a + (b - a) * clamped_t;
        };
        for (size_t component = 0; component < result.position.size(); ++component) {
            result.position[component] = lerp(
                    from.position[component], to.position[component]);
        }
        result.vertex = from.vertex;
        result.vertex.position_x = static_cast<float>(lerp(
                from.vertex.position_x, to.vertex.position_x));
        result.vertex.position_y = static_cast<float>(lerp(
                from.vertex.position_y, to.vertex.position_y));
        result.vertex.position_z = static_cast<float>(lerp(
                from.vertex.position_z, to.vertex.position_z));
        result.vertex.normal_x = static_cast<float>(lerp(
                from.vertex.normal_x, to.vertex.normal_x));
        result.vertex.normal_y = static_cast<float>(lerp(
                from.vertex.normal_y, to.vertex.normal_y));
        result.vertex.normal_z = static_cast<float>(lerp(
                from.vertex.normal_z, to.vertex.normal_z));
        result.vertex.uv_x = static_cast<float>(lerp(
                from.vertex.uv_x, to.vertex.uv_x));
        result.vertex.uv_y = static_cast<float>(lerp(
                from.vertex.uv_y, to.vertex.uv_y));
        result.vertex.color_r = static_cast<float>(lerp(
                from.vertex.color_r, to.vertex.color_r));
        result.vertex.color_g = static_cast<float>(lerp(
                from.vertex.color_g, to.vertex.color_g));
        result.vertex.color_b = static_cast<float>(lerp(
                from.vertex.color_b, to.vertex.color_b));
        result.vertex.color_a = static_cast<float>(lerp(
                from.vertex.color_a, to.vertex.color_a));
        return result;
    };
    const auto clip_plane_distance = [](
            const std::array<double, 4> &position, size_t plane) {
        switch (plane) {
            case 0: return position[0] + position[3]; // left
            case 1: return position[3] - position[0]; // right
            case 2: return position[1] + position[3]; // bottom
            case 3: return position[3] - position[1]; // top
            case 4: return position[2] + position[3]; // near
            default: return position[3] - position[2]; // far
        }
    };

    projected_scene.surfaces.clear();
    std::vector<bool> draw_used(scene.fast3d.draws.size(), false);
    uint32_t last_draw_index = FAST3D_CAPTURE_NO_INDEX;
    for (const ModelSurface &source_surface : scene.surfaces) {
        ModelSurface target_surface = source_surface;
        target_surface.vertices.clear();
        target_surface.indices.clear();
        target_surface.triangle_draw_indices.clear();
        const size_t triangle_count = source_surface.indices.size() / 3U;
        for (size_t triangle = 0; triangle < triangle_count; ++triangle) {
            const uint32_t draw_index = any_associations ?
                    source_surface.triangle_draw_indices[triangle] :
                    static_cast<uint32_t>(scene.fast3d.draws.size() - 1U);
            ProjectionState *state = resolve_state(draw_index);
            if (state == nullptr) {
                return fail("invalid_or_missing_fast3d_draw_state");
            }
            const Fast3DDrawStateCapture &draw = scene.fast3d.draws[draw_index];
            std::vector<ClipVertex> clipped_polygon;
            clipped_polygon.reserve(9U);
            bool invalid_triangle = false;
            for (size_t corner = 0; corner < 3U; ++corner) {
                const uint32_t source_index =
                        source_surface.indices[triangle * 3U + corner];
                if (source_index >= source_surface.vertices.size()) {
                    return fail("invalid_triangle_index");
                }
                ModelVertex vertex = source_surface.vertices[source_index];
                const bool has_any_vertex_state =
                        vertex.fast3d_modelview_matrix_index != FAST3D_CAPTURE_NO_INDEX ||
                        vertex.fast3d_projection_matrix_index != FAST3D_CAPTURE_NO_INDEX ||
                        vertex.fast3d_viewport_index != FAST3D_CAPTURE_NO_INDEX;
                const bool has_vertex_state =
                        vertex.fast3d_modelview_matrix_index != FAST3D_CAPTURE_NO_INDEX &&
                        vertex.fast3d_projection_matrix_index != FAST3D_CAPTURE_NO_INDEX &&
                        vertex.fast3d_viewport_index != FAST3D_CAPTURE_NO_INDEX;
                if (has_any_vertex_state && !has_vertex_state) {
                    return fail("incomplete_fast3d_vertex_load_state");
                }
                ProjectionState *vertex_state = state;
                if (has_vertex_state) {
                    vertex_state = resolve_state_indices(
                            vertex.fast3d_modelview_matrix_index,
                            vertex.fast3d_projection_matrix_index,
                            vertex.fast3d_viewport_index);
                    if (vertex_state == nullptr) {
                        return fail("invalid_fast3d_vertex_load_state");
                    }
                    summary.uses_vertex_load_state = true;
                    ++summary.vertex_load_state_vertex_count;
                } else {
                    ++summary.triangle_state_fallback_vertex_count;
                }
                const std::array<double, 4> model_position = {
                        static_cast<double>(vertex.position_x),
                        static_cast<double>(vertex.position_y),
                        static_cast<double>(vertex.position_z), 1.0};
                const std::array<double, 4> view_position =
                        multiply(model_position, vertex_state->modelview);
                const std::array<double, 4> clip_position =
                        multiply(view_position, vertex_state->projection);
                if (!std::isfinite(clip_position[0]) ||
                        !std::isfinite(clip_position[1]) ||
                        !std::isfinite(clip_position[2]) ||
                        !std::isfinite(clip_position[3])) {
                    ++summary.invalid_vertex_count;
                    invalid_triangle = true;
                    break;
                }
                ++summary.transformed_vertex_count;
                if (clip_position[3] <= 1.0e-9) {
                    ++summary.behind_camera_vertex_count;
                    ++summary.clipped_vertex_count;
                } else {
                    const double ndc_x = clip_position[0] / clip_position[3];
                    const double ndc_y = clip_position[1] / clip_position[3];
                    const double ndc_z = clip_position[2] / clip_position[3];
                    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) ||
                            !std::isfinite(ndc_z)) {
                        ++summary.invalid_vertex_count;
                        invalid_triangle = true;
                        break;
                    }
                    const bool visible = std::abs(ndc_x) <= 1.0 &&
                            std::abs(ndc_y) <= 1.0 && std::abs(ndc_z) <= 1.0;
                    if (visible) ++summary.visible_vertex_count;
                    else ++summary.clipped_vertex_count;
                }
                ClipVertex clipped_vertex;
                clipped_vertex.vertex = vertex;
                clipped_vertex.position = clip_position;
                clipped_polygon.push_back(clipped_vertex);
            }
            if (invalid_triangle) {
                ++summary.rejected_triangle_count;
                continue;
            }

            bool polygon_changed = false;
            for (size_t plane = 0; plane < 6U && !clipped_polygon.empty(); ++plane) {
                std::vector<ClipVertex> output;
                output.reserve(clipped_polygon.size() + 1U);
                ClipVertex previous = clipped_polygon.back();
                double previous_distance = clip_plane_distance(previous.position, plane);
                bool previous_inside = previous_distance >= 0.0;
                for (const ClipVertex &current : clipped_polygon) {
                    const double current_distance =
                            clip_plane_distance(current.position, plane);
                    const bool current_inside = current_distance >= 0.0;
                    if (previous_inside != current_inside) {
                        const double denominator = previous_distance - current_distance;
                        if (std::abs(denominator) > 1.0e-12) {
                            output.push_back(interpolate_clip_vertex(
                                    previous, current, previous_distance / denominator));
                            polygon_changed = true;
                        }
                    }
                    if (current_inside) output.push_back(current);
                    previous = current;
                    previous_distance = current_distance;
                    previous_inside = current_inside;
                }
                clipped_polygon = std::move(output);
            }
            if (clipped_polygon.size() < 3U) {
                ++summary.rejected_triangle_count;
                continue;
            }
            bool invalid_clipped_polygon = false;
            for (const ClipVertex &vertex : clipped_polygon) {
                if (!std::isfinite(vertex.position[0]) ||
                        !std::isfinite(vertex.position[1]) ||
                        !std::isfinite(vertex.position[2]) ||
                        !std::isfinite(vertex.position[3]) ||
                        vertex.position[3] <= 1.0e-9) {
                    invalid_clipped_polygon = true;
                    break;
                }
            }
            if (invalid_clipped_polygon) {
                ++summary.rejected_triangle_count;
                continue;
            }
            if (polygon_changed || clipped_polygon.size() != 3U) {
                ++summary.clipped_triangle_count;
            }
            for (size_t fan = 1U; fan + 1U < clipped_polygon.size(); ++fan) {
                const std::array<ClipVertex, 3> clipped_triangle = {
                        clipped_polygon[0], clipped_polygon[fan],
                        clipped_polygon[fan + 1U]};
                const auto ndc_xy = [](const ClipVertex &vertex) {
                    return std::array<double, 2>{
                            vertex.position[0] / vertex.position[3],
                            vertex.position[1] / vertex.position[3]};
                };
                const auto first_xy = ndc_xy(clipped_triangle[0]);
                const auto second_xy = ndc_xy(clipped_triangle[1]);
                const auto third_xy = ndc_xy(clipped_triangle[2]);
                const double signed_area =
                        (second_xy[0] - first_xy[0]) *
                                (third_xy[1] - first_xy[1]) -
                        (second_xy[1] - first_xy[1]) *
                                (third_xy[0] - first_xy[0]);
                // The captured F3DEX viewport and projection preserve positive
                // signed area as the task's front-facing winding in Godot NDC.
                const bool front_facing = signed_area > 1.0e-12;
                const bool degenerate = std::abs(signed_area) <= 1.0e-12;
                if ((source_surface.cull_front && front_facing) ||
                        (source_surface.cull_back && !front_facing) ||
                        (degenerate && (source_surface.cull_front ||
                                source_surface.cull_back))) {
                    ++summary.culled_triangle_count;
                    continue;
                }
                for (const ClipVertex &clipped_vertex : clipped_triangle) {
                    ModelVertex vertex = clipped_vertex.vertex;
                    const double ndc_x = clipped_vertex.position[0] /
                            clipped_vertex.position[3];
                    const double ndc_y = clipped_vertex.position[1] /
                            clipped_vertex.position[3];
                    const double ndc_z = clipped_vertex.position[2] /
                            clipped_vertex.position[3];
                    const double screen_x = (ndc_x *
                            static_cast<double>(state->viewport->scale[0]) +
                            static_cast<double>(state->viewport->translate[0])) / 4.0;
                    const double screen_y = (ndc_y *
                            static_cast<double>(state->viewport->scale[1]) +
                            static_cast<double>(state->viewport->translate[1])) / 4.0;
                    vertex.position_x = static_cast<float>(ndc_x);
                    vertex.position_y = static_cast<float>(ndc_y);
                    // N64/OpenGL NDC uses -1 near and +1 far. Godot's camera is
                    // positioned on +Z looking toward -Z, so negate clip Z to
                    // preserve the captured near/far ordering in world space.
                    vertex.position_z = static_cast<float>(-ndc_z);
                    if (!summary.has_screen_bounds) {
                        summary.has_screen_bounds = true;
                        summary.min_screen_x = static_cast<float>(screen_x);
                        summary.max_screen_x = static_cast<float>(screen_x);
                        summary.min_screen_y = static_cast<float>(screen_y);
                        summary.max_screen_y = static_cast<float>(screen_y);
                    } else {
                        summary.min_screen_x = std::min(summary.min_screen_x,
                                static_cast<float>(screen_x));
                        summary.max_screen_x = std::max(summary.max_screen_x,
                                static_cast<float>(screen_x));
                        summary.min_screen_y = std::min(summary.min_screen_y,
                                static_cast<float>(screen_y));
                        summary.max_screen_y = std::max(summary.max_screen_y,
                                static_cast<float>(screen_y));
                    }
                    target_surface.vertices.push_back(vertex);
                    target_surface.indices.push_back(static_cast<uint32_t>(
                            target_surface.vertices.size() - 1U));
                }
                target_surface.triangle_draw_indices.push_back(draw_index);
                ++summary.transformed_triangle_count;
            }
            draw_used[draw_index] = true;
            last_draw_index = draw_index;
            summary.modelview_matrix_index = draw.modelview_matrix_index;
            summary.projection_matrix_index = draw.projection_matrix_index;
            summary.viewport_index = draw.viewport_index;
            summary.viewport_width = state->width;
            summary.viewport_height = state->height;
        }
        projected_scene.surfaces.push_back(std::move(target_surface));
    }
    for (bool used : draw_used) {
        if (used) ++summary.draw_state_count_used;
    }
    summary.draw_index = last_draw_index;
    summary.available = true;
    summary.reason = summary.uses_vertex_load_state ?
            "raw_fast3d_projection_per_vertex_load_state" :
            (any_associations ?
                    "raw_fast3d_projection_per_draw_legacy_fallback" :
                    "raw_fast3d_projection_latest_draw_legacy_fallback");
    error.clear();
    return true;
}


bool project_render_scene_fast3d_camera_state_group(
        const RenderSceneData &scene,
        uint32_t projection_matrix_index,
        uint32_t viewport_index,
        RenderSceneData &group_scene,
        uint32_t &selected_triangle_count,
        uint32_t &selected_vertex_count,
        uint32_t &invalid_vertex_state_triangle_count,
        uint32_t &mixed_state_triangle_count,
        std::string &error) {
    group_scene = {};
    selected_triangle_count = 0U;
    selected_vertex_count = 0U;
    invalid_vertex_state_triangle_count = 0U;
    mixed_state_triangle_count = 0U;
    if (projection_matrix_index == FAST3D_CAPTURE_NO_INDEX ||
            viewport_index == FAST3D_CAPTURE_NO_INDEX) {
        error = "invalid_camera_state_group_indices";
        return false;
    }
    error.clear();

    group_scene.flat_reference_texture_index = scene.flat_reference_texture_index;
    group_scene.textures = scene.textures;
    group_scene.fast3d = scene.fast3d;
    group_scene.surfaces.reserve(scene.surfaces.size());

    for (const ModelSurface &source_surface : scene.surfaces) {
        if (source_surface.indices.size() % 3U != 0U ||
                source_surface.triangle_draw_indices.size() !=
                        source_surface.indices.size() / 3U) {
            error = "invalid_camera_state_group_triangle_associations";
            return false;
        }

        for (size_t triangle = 0U;
                triangle < source_surface.triangle_draw_indices.size(); ++triangle) {
            const uint32_t draw_index =
                    source_surface.triangle_draw_indices[triangle];
            ModelSurface selected_surface = source_surface;
            selected_surface.vertices.clear();
            selected_surface.indices.clear();
            selected_surface.triangle_draw_indices.clear();
            bool has_invalid_state = false;
            bool has_matching_vertex = false;
            bool matches_group = true;
            for (size_t corner = 0U; corner < 3U; ++corner) {
                const uint32_t source_vertex_index =
                        source_surface.indices[triangle * 3U + corner];
                if (source_vertex_index >= source_surface.vertices.size()) {
                    has_invalid_state = true;
                    ++invalid_vertex_state_triangle_count;
                    break;
                }
                const ModelVertex &source_vertex =
                        source_surface.vertices[source_vertex_index];
                if (source_vertex.fast3d_projection_matrix_index ==
                                FAST3D_CAPTURE_NO_INDEX ||
                        source_vertex.fast3d_viewport_index ==
                                FAST3D_CAPTURE_NO_INDEX) {
                    has_invalid_state = true;
                    ++invalid_vertex_state_triangle_count;
                    break;
                }
                const bool vertex_matches =
                        source_vertex.fast3d_projection_matrix_index ==
                                projection_matrix_index &&
                        source_vertex.fast3d_viewport_index ==
                                viewport_index;
                has_matching_vertex = has_matching_vertex || vertex_matches;
                matches_group = matches_group && vertex_matches;
                if (vertex_matches) {
                    selected_surface.vertices.push_back(source_vertex);
                    selected_surface.indices.push_back(static_cast<uint32_t>(
                            selected_surface.vertices.size() - 1U));
                }
            }
            if (has_invalid_state) {
                continue;
            }
            if (!has_matching_vertex || !matches_group) {
                ++mixed_state_triangle_count;
                continue;
            }
            selected_surface.triangle_draw_indices.push_back(draw_index);
            group_scene.surfaces.push_back(std::move(selected_surface));
            ++selected_triangle_count;
            selected_vertex_count += 3U;
        }
    }

    if (selected_triangle_count == 0U) {
        error = "camera_state_group_selection_contains_no_geometry";
        return false;
    }
    error.clear();
    return true;
}
bool inspect_render_scene_fast3d_camera_metadata(
        const RenderSceneData &scene,
        uint32_t first_draw_index,
        uint32_t last_draw_index,
        Fast3DCameraMetadata &metadata,
        std::string &error) {
    metadata = {};
    metadata.draw_first = first_draw_index;
    metadata.draw_last = last_draw_index;
    const auto fail = [&](const char *reason) {
        metadata.reason = reason;
        error = reason;
        return false;
    };
    if (first_draw_index > last_draw_index ||
            last_draw_index >= scene.fast3d.draws.size()) {
        return fail("invalid_camera_metadata_draw_range");
    }
    if (scene.fast3d.matrix_confidence != Fast3DStateConfidence::Captured ||
            scene.fast3d.viewport_confidence !=
                    Fast3DStateConfidence::Captured ||
            scene.fast3d.draw_state_confidence !=
                    Fast3DStateConfidence::Captured) {
        return fail("missing_captured_fast3d_camera_state");
    }

    using GroupKey = std::array<uint32_t, 2>;
    struct GroupStats {
        uint32_t triangle_count = 0;
        uint32_t vertex_count = 0;
    };
    std::map<GroupKey, GroupStats> groups;
    for (const ModelSurface &surface : scene.surfaces) {
        if (surface.indices.size() % 3U != 0U ||
                surface.triangle_draw_indices.size() !=
                        surface.indices.size() / 3U) {
            return fail("invalid_triangle_draw_associations");
        }
        for (size_t triangle = 0;
                triangle < surface.triangle_draw_indices.size(); ++triangle) {
            const uint32_t draw_index =
                    surface.triangle_draw_indices[triangle];
            if (draw_index < first_draw_index || draw_index > last_draw_index) {
                continue;
            }
            ++metadata.selected_triangle_count;
            metadata.selected_vertex_count += 3U;
            std::array<GroupKey, 3> triangle_groups{};
            std::array<bool, 3> triangle_group_valid{};
            for (size_t corner = 0; corner < 3U; ++corner) {
                const uint32_t vertex_index =
                        surface.indices[triangle * 3U + corner];
                if (vertex_index >= surface.vertices.size()) {
                    return fail("invalid_camera_metadata_vertex_index");
                }
                const ModelVertex &vertex = surface.vertices[vertex_index];
                const uint32_t projection_index =
                        vertex.fast3d_projection_matrix_index;
                const uint32_t viewport_index =
                        vertex.fast3d_viewport_index;
                if (projection_index == FAST3D_CAPTURE_NO_INDEX ||
                        viewport_index == FAST3D_CAPTURE_NO_INDEX ||
                        projection_index >= scene.fast3d.matrices.size() ||
                        viewport_index >= scene.fast3d.viewports.size() ||
                        !scene.fast3d.matrices[projection_index].projection) {
                    ++metadata.invalid_vertex_state_count;
                    continue;
                }
                const GroupKey key = {projection_index, viewport_index};
                triangle_groups[corner] = key;
                triangle_group_valid[corner] = true;
                ++groups[key].vertex_count;
                ++metadata.associated_vertex_state_count;
            }
            for (size_t corner = 0; corner < 3U; ++corner) {
                if (!triangle_group_valid[corner]) continue;
                bool already_counted = false;
                for (size_t previous = 0; previous < corner; ++previous) {
                    already_counted = already_counted ||
                            (triangle_group_valid[previous] &&
                                    triangle_groups[previous] ==
                                            triangle_groups[corner]);
                }
                if (!already_counted) {
                    ++groups[triangle_groups[corner]].triangle_count;
                }
            }
        }
    }

    metadata.selected_draw_role =
            metadata.selected_triangle_count == 0U ?
            Fast3DDrawRole::Unknown : Fast3DDrawRole::WorldGeometry;
    metadata.draw_role_reason =
            metadata.selected_triangle_count == 0U ?
            "no_geometry_in_selected_draw_range" :
            "explicit_geometry_selected_by_inclusive_draw_range";
    if (groups.empty()) {
        metadata.reason = metadata.selected_triangle_count == 0U ?
                "no_geometry_in_selected_draw_range" :
                "no_geometry_associated_vertex_load_camera_state";
        error.clear();
        return true;
    }

    const auto find_perspective_load = [&scene](
            uint32_t matrix_index, uint32_t &load_index) {
        for (size_t depth = 0; depth <= scene.fast3d.matrices.size(); ++depth) {
            if (matrix_index >= scene.fast3d.matrices.size()) return false;
            const Fast3DMatrixCapture &capture =
                    scene.fast3d.matrices[matrix_index];
            if (!capture.projection) return false;
            if (capture.load ||
                    capture.previous_matrix_index == FAST3D_CAPTURE_NO_INDEX) {
                load_index = matrix_index;
                return true;
            }
            matrix_index = capture.previous_matrix_index;
        }
        return false;
    };

    uint32_t dominant_vertex_count = 0;
    for (const auto &[key, stats] : groups) {
        Fast3DCameraStateGroup group;
        group.projection_matrix_index = key[0];
        group.viewport_index = key[1];
        group.triangle_count = stats.triangle_count;
        group.vertex_count = stats.vertex_count;
        group.selected_vertex_share =
                metadata.selected_vertex_count == 0U ? 0.0 :
                static_cast<double>(stats.vertex_count) /
                        static_cast<double>(metadata.selected_vertex_count);
        group.draw_role = Fast3DDrawRole::WorldGeometry;
        find_perspective_load(
                group.projection_matrix_index,
                group.perspective_matrix_index);
        metadata.state_groups.push_back(group);
        const uint32_t group_index =
                static_cast<uint32_t>(metadata.state_groups.size() - 1U);
        if (stats.vertex_count > dominant_vertex_count) {
            dominant_vertex_count = stats.vertex_count;
            metadata.dominant_group_index = group_index;
        }
    }
    metadata.dominant_vertex_share =
            metadata.selected_vertex_count == 0U ? 0.0 :
            static_cast<double>(dominant_vertex_count) /
                    static_cast<double>(metadata.selected_vertex_count);
    metadata.available = true;
    metadata.view_space_camera_pose_available = true;
    metadata.reason = "dominant_geometry_projection_viewport_candidate";

    const Fast3DCameraStateGroup &dominant =
            metadata.state_groups[metadata.dominant_group_index];
    const Fast3DViewportCapture &viewport =
            scene.fast3d.viewports[dominant.viewport_index];
    metadata.viewport_width = static_cast<uint32_t>(
            std::abs(static_cast<int32_t>(viewport.scale[0])) / 2);
    metadata.viewport_height = static_cast<uint32_t>(
            std::abs(static_cast<int32_t>(viewport.scale[1])) / 2);
    if (dominant.perspective_matrix_index == FAST3D_CAPTURE_NO_INDEX) {
        metadata.perspective_reason =
                "projection_chain_has_no_valid_load_matrix";
        error.clear();
        return true;
    }

    const Fast3DMatrixCapture &capture =
            scene.fast3d.matrices[dominant.perspective_matrix_index];
    std::array<double, 16> matrix{};
    for (size_t index = 0; index < matrix.size(); ++index) {
        matrix[index] =
                static_cast<double>(capture.fixed_16_16[index]) / 65536.0;
        if (!std::isfinite(matrix[index])) {
            metadata.perspective_reason =
                    "projection_matrix_contains_non_finite_value";
            error.clear();
            return true;
        }
    }
    constexpr double tolerance = 1.0e-3;
    // Homogeneous projection matrices are equivalent up to a non-zero scalar.
    // Banjo commonly captures the conventional N64 perspective matrix at
    // half scale (Wz = -0.5 instead of -1.0). Normalize that scale before
    // classifying the matrix or deriving its lens parameters.
    const double homogeneous_scale = -matrix[11];
    if (!std::isfinite(homogeneous_scale) ||
            homogeneous_scale <= tolerance) {
        metadata.perspective_reason =
                "projection_has_invalid_homogeneous_scale";
        error.clear();
        return true;
    }
    for (double &value : matrix) value /= homogeneous_scale;
    const auto near_zero = [&](size_t index) {
        return std::abs(matrix[index]) <= tolerance;
    };
    const bool symmetric_perspective =
            matrix[0] > tolerance && matrix[5] > tolerance &&
            near_zero(1) && near_zero(2) && near_zero(3) &&
            near_zero(4) && near_zero(6) && near_zero(7) &&
            near_zero(8) && near_zero(9) &&
            std::abs(matrix[11] + 1.0) <= tolerance &&
            near_zero(12) && near_zero(13) && near_zero(15);
    if (!symmetric_perspective) {
        metadata.perspective_reason =
                "dominant_projection_is_not_symmetric_negative_z_perspective";
        error.clear();
        return true;
    }

    const double vertical_fov =
            2.0 * std::atan(1.0 / matrix[5]);
    const double aspect = matrix[5] / matrix[0];
    const double near_denominator = matrix[10] - 1.0;
    const double far_denominator = matrix[10] + 1.0;
    if (std::abs(near_denominator) <= tolerance ||
            std::abs(far_denominator) <= tolerance) {
        metadata.perspective_reason =
                "perspective_near_far_are_not_finite";
        error.clear();
        return true;
    }
    const double near_plane = matrix[14] / near_denominator;
    const double far_plane = matrix[14] / far_denominator;
    const double vertical_fov_degrees =
            vertical_fov * 180.0 / std::acos(-1.0);
    if (!std::isfinite(vertical_fov_degrees) ||
            !std::isfinite(aspect) || !std::isfinite(near_plane) ||
            !std::isfinite(far_plane) ||
            vertical_fov_degrees <= 1.0 || vertical_fov_degrees >= 179.0 ||
            aspect <= 0.0 || near_plane <= 0.0 ||
            far_plane <= near_plane) {
        metadata.perspective_reason =
                "perspective_parameters_failed_validity_checks";
        error.clear();
        return true;
    }
    metadata.perspective_parameters_available = true;
    metadata.vertical_fov_degrees = vertical_fov_degrees;
    metadata.aspect_ratio = aspect;
    metadata.near_plane = near_plane;
    metadata.far_plane = far_plane;
    metadata.perspective_reason =
            "symmetric_negative_z_perspective_derived_from_captured_load_matrix";
    error.clear();
    return true;
}

bool reconstruct_render_scene_fast3d_view_space(
        const RenderSceneData &scene,
        RenderSceneData &view_scene,
        Fast3DProjectionSummary &summary,
        std::string &error) {
    view_scene = {};
    summary = {};
    summary.microcode_family = scene.fast3d.microcode_family;
    summary.gbi_command_count = scene.fast3d.command_count;
    summary.gbi_no_op_command_count = scene.fast3d.no_op_command_count;
    summary.gbi_passthrough_command_count =
            scene.fast3d.passthrough_command_count;
    summary.gbi_passthrough_opcodes = scene.fast3d.passthrough_opcodes;
    summary.camera_interpretation_verified =
            scene.fast3d.camera_confidence == Fast3DStateConfidence::Captured;
    summary.draw_count = static_cast<uint32_t>(scene.fast3d.draws.size());

    const auto fail = [&](const char *reason) {
        summary.reason = reason;
        error = reason;
        view_scene = {};
        return false;
    };
    if (scene.surfaces.empty()) {
        return fail("no_renderable_scene_geometry");
    }
    if (scene.fast3d.matrix_confidence != Fast3DStateConfidence::Captured ||
            scene.fast3d.matrices.empty()) {
        return fail("missing_captured_fast3d_matrices");
    }
    if (scene.fast3d.draw_state_confidence !=
                    Fast3DStateConfidence::Captured ||
            scene.fast3d.draws.empty()) {
        return fail("missing_captured_fast3d_draw_state");
    }

    bool any_associations = false;
    bool missing_associations = false;
    for (const ModelSurface &surface : scene.surfaces) {
        if (surface.indices.size() % 3U != 0U) {
            return fail("scene_topology_is_not_triangles");
        }
        const size_t triangle_count = surface.indices.size() / 3U;
        if (surface.triangle_draw_indices.empty()) {
            missing_associations = missing_associations || triangle_count != 0U;
            continue;
        }
        if (surface.triangle_draw_indices.size() != triangle_count) {
            return fail("invalid_triangle_draw_association_count");
        }
        any_associations = true;
        for (uint32_t draw_index : surface.triangle_draw_indices) {
            if (draw_index >= scene.fast3d.draws.size()) {
                return fail("invalid_triangle_draw_association");
            }
        }
    }
    if (any_associations && missing_associations) {
        return fail("incomplete_triangle_draw_association");
    }
    summary.uses_per_draw_state = any_associations;
    summary.uses_latest_draw_state = !any_associations;

    const auto capture_matrix = [](const Fast3DMatrixCapture &capture) {
        std::array<double, 16> matrix{};
        for (size_t index = 0; index < matrix.size(); ++index) {
            matrix[index] =
                    static_cast<double>(capture.fixed_16_16[index]) / 65536.0;
        }
        return matrix;
    };
    const auto multiply_matrices = [](
            const std::array<double, 16> &lhs,
            const std::array<double, 16> &rhs) {
        std::array<double, 16> result{};
        for (size_t row = 0; row < 4U; ++row) {
            for (size_t column = 0; column < 4U; ++column) {
                for (size_t inner = 0; inner < 4U; ++inner) {
                    result[row * 4U + column] +=
                            lhs[row * 4U + inner] *
                            rhs[inner * 4U + column];
                }
            }
        }
        return result;
    };
    const auto identity_matrix = []() {
        std::array<double, 16> matrix{};
        for (size_t index = 0; index < 4U; ++index) {
            matrix[index * 4U + index] = 1.0;
        }
        return matrix;
    };
    const auto resolve_modelview = [&](uint32_t matrix_index,
            std::array<double, 16> &resolved) {
        bool first = true;
        while (matrix_index != FAST3D_CAPTURE_NO_INDEX) {
            if (matrix_index >= scene.fast3d.matrices.size()) return false;
            const Fast3DMatrixCapture &capture =
                    scene.fast3d.matrices[matrix_index];
            if (capture.projection) return false;
            const std::array<double, 16> local = capture_matrix(capture);
            resolved = first ? local : multiply_matrices(resolved, local);
            first = false;
            if (capture.load) break;
            matrix_index = capture.previous_matrix_index;
        }
        return !first;
    };
    const auto resolve_projection_view = [&](uint32_t matrix_index,
            std::array<double, 16> &resolved) {
        resolved = identity_matrix();
        bool has_multiplier = false;
        while (matrix_index != FAST3D_CAPTURE_NO_INDEX) {
            if (matrix_index >= scene.fast3d.matrices.size()) return false;
            const Fast3DMatrixCapture &capture =
                    scene.fast3d.matrices[matrix_index];
            if (!capture.projection) return false;
            if (capture.load ||
                    capture.previous_matrix_index == FAST3D_CAPTURE_NO_INDEX) return true;
            const std::array<double, 16> local = capture_matrix(capture);
            resolved = has_multiplier ?
                    multiply_matrices(resolved, local) : local;
            has_multiplier = true;
            matrix_index = capture.previous_matrix_index;
        }
        return false;
    };
    const auto multiply = [](const std::array<double, 4> &vector,
            const std::array<double, 16> &matrix) {
        std::array<double, 4> result{};
        for (size_t column = 0; column < 4U; ++column) {
            for (size_t row = 0; row < 4U; ++row) {
                result[column] += vector[row] * matrix[row * 4U + column];
            }
        }
        return result;
    };

    struct ViewState {
        bool ready = false;
        std::array<double, 16> modelview{};
        std::array<double, 16> projection_view{};
    };
    std::map<std::array<uint32_t, 2>, ViewState> states;
    const auto resolve_state_indices = [&](uint32_t modelview_matrix_index,
            uint32_t projection_matrix_index) -> ViewState * {
        const std::array<uint32_t, 2> key = {
                modelview_matrix_index, projection_matrix_index};
        ViewState &state = states[key];
        if (state.ready) return &state;
        if (modelview_matrix_index == FAST3D_CAPTURE_NO_INDEX ||
                modelview_matrix_index >= scene.fast3d.matrices.size() ||
                projection_matrix_index == FAST3D_CAPTURE_NO_INDEX ||
                projection_matrix_index >= scene.fast3d.matrices.size()) {
            return nullptr;
        }
        if (!resolve_modelview(modelview_matrix_index, state.modelview) ||
                !resolve_projection_view(
                        projection_matrix_index, state.projection_view)) {
            return nullptr;
        }
        state.ready = true;
        return &state;
    };

    view_scene = scene;
    view_scene.surfaces.clear();
    std::vector<bool> draw_used(scene.fast3d.draws.size(), false);
    uint32_t last_draw_index = FAST3D_CAPTURE_NO_INDEX;
    for (const ModelSurface &source_surface : scene.surfaces) {
        ModelSurface target_surface = source_surface;
        target_surface.vertices.clear();
        target_surface.indices.clear();
        target_surface.triangle_draw_indices.clear();
        const size_t triangle_count = source_surface.indices.size() / 3U;
        for (size_t triangle = 0; triangle < triangle_count; ++triangle) {
            const uint32_t draw_index = any_associations ?
                    source_surface.triangle_draw_indices[triangle] :
                    static_cast<uint32_t>(scene.fast3d.draws.size() - 1U);
            const Fast3DDrawStateCapture &draw = scene.fast3d.draws[draw_index];
            ViewState *draw_state = resolve_state_indices(
                    draw.modelview_matrix_index,
                    draw.projection_matrix_index);
            if (draw_state == nullptr) {
                return fail("invalid_or_missing_fast3d_draw_state");
            }
            std::array<ModelVertex, 3> transformed{};
            bool invalid_triangle = false;
            for (size_t corner = 0; corner < 3U; ++corner) {
                const uint32_t source_index =
                        source_surface.indices[triangle * 3U + corner];
                if (source_index >= source_surface.vertices.size()) {
                    return fail("invalid_triangle_index");
                }
                ModelVertex vertex = source_surface.vertices[source_index];
                const bool has_any_vertex_state =
                        vertex.fast3d_modelview_matrix_index != FAST3D_CAPTURE_NO_INDEX ||
                        vertex.fast3d_projection_matrix_index != FAST3D_CAPTURE_NO_INDEX ||
                        vertex.fast3d_viewport_index != FAST3D_CAPTURE_NO_INDEX;
                const bool has_vertex_state =
                        vertex.fast3d_modelview_matrix_index != FAST3D_CAPTURE_NO_INDEX &&
                        vertex.fast3d_projection_matrix_index != FAST3D_CAPTURE_NO_INDEX &&
                        vertex.fast3d_viewport_index != FAST3D_CAPTURE_NO_INDEX;
                if (has_any_vertex_state && !has_vertex_state) {
                    return fail("incomplete_fast3d_vertex_load_state");
                }
                ViewState *vertex_state = draw_state;
                if (has_vertex_state) {
                    vertex_state = resolve_state_indices(
                            vertex.fast3d_modelview_matrix_index,
                            vertex.fast3d_projection_matrix_index);
                    if (vertex_state == nullptr) {
                        return fail("invalid_fast3d_vertex_load_state");
                    }
                    summary.uses_vertex_load_state = true;
                    ++summary.vertex_load_state_vertex_count;
                } else {
                    ++summary.triangle_state_fallback_vertex_count;
                }
                const std::array<double, 4> model_position = {
                        static_cast<double>(vertex.position_x),
                        static_cast<double>(vertex.position_y),
                        static_cast<double>(vertex.position_z), 1.0};
                const std::array<double, 4> modelview_position =
                        multiply(model_position, vertex_state->modelview);
                const std::array<double, 4> view_position =
                        multiply(modelview_position, vertex_state->projection_view);
                if (!std::isfinite(view_position[0]) ||
                        !std::isfinite(view_position[1]) ||
                        !std::isfinite(view_position[2]) ||
                        !std::isfinite(view_position[3]) ||
                        std::abs(view_position[3]) <= 1.0e-9) {
                    ++summary.invalid_vertex_count;
                    invalid_triangle = true;
                    break;
                }
                vertex.position_x = static_cast<float>(
                        view_position[0] / view_position[3]);
                vertex.position_y = static_cast<float>(
                        view_position[1] / view_position[3]);
                vertex.position_z = static_cast<float>(
                        view_position[2] / view_position[3]);
                transformed[corner] = vertex;
                ++summary.transformed_vertex_count;
            }
            if (invalid_triangle) {
                ++summary.rejected_triangle_count;
                continue;
            }
            for (const ModelVertex &vertex : transformed) {
                target_surface.vertices.push_back(vertex);
                target_surface.indices.push_back(static_cast<uint32_t>(
                        target_surface.vertices.size() - 1U));
            }
            target_surface.triangle_draw_indices.push_back(draw_index);
            draw_used[draw_index] = true;
            last_draw_index = draw_index;
            summary.modelview_matrix_index = draw.modelview_matrix_index;
            summary.projection_matrix_index = draw.projection_matrix_index;
            summary.viewport_index = draw.viewport_index;
            ++summary.transformed_triangle_count;
        }
        view_scene.surfaces.push_back(std::move(target_surface));
    }
    for (bool used : draw_used) {
        if (used) ++summary.draw_state_count_used;
    }
    summary.draw_index = last_draw_index;
    summary.available = true;
    summary.reason = summary.uses_vertex_load_state ?
            "raw_fast3d_view_space_projection_load_stripped_per_vertex_load_state" :
            (any_associations ?
                    "raw_fast3d_view_space_projection_load_stripped_per_draw_fallback" :
                    "raw_fast3d_view_space_projection_load_stripped_latest_draw_fallback");
    error.clear();
    return true;
}
} // namespace xr64

