#include "fast3d_scene_decoder.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>

namespace xr64 {
namespace {

constexpr uint8_t COMMAND_MOVE_MEMORY = 0x03;
constexpr uint8_t COMMAND_MATRIX = 0x01;
constexpr uint8_t COMMAND_VERTEX = 0x04;
constexpr uint8_t COMMAND_DISPLAY_LIST = 0x06;
constexpr uint8_t COMMAND_TRIANGLE_1 = 0xBF;
constexpr uint8_t COMMAND_TRIANGLE_2 = 0xB1;
constexpr uint8_t COMMAND_POP_MATRIX = 0xBD;
constexpr uint8_t COMMAND_CLEAR_GEOMETRY_MODE = 0xB6;
constexpr uint8_t COMMAND_SET_GEOMETRY_MODE = 0xB7;
constexpr uint8_t COMMAND_END_DISPLAY_LIST = 0xB8;
constexpr uint8_t COMMAND_SET_OTHER_MODE_L = 0xB9;
constexpr uint8_t COMMAND_SET_OTHER_MODE_H = 0xBA;
constexpr uint8_t COMMAND_TEXTURE = 0xBB;
constexpr uint8_t COMMAND_MOVE_WORD = 0xBC;
constexpr uint8_t MOVE_WORD_SEGMENT = 0x06;
constexpr uint8_t COMMAND_SET_COMBINE = 0xFC;
constexpr uint8_t COMMAND_LOAD_TLUT = 0xF0;
constexpr uint8_t COMMAND_SET_TILE_SIZE = 0xF2;
constexpr uint8_t COMMAND_LOAD_BLOCK = 0xF3;
constexpr uint8_t COMMAND_LOAD_TILE = 0xF4;
constexpr uint8_t COMMAND_SET_TILE = 0xF5;
constexpr uint8_t COMMAND_SET_TEXTURE_IMAGE = 0xFD;
constexpr uint8_t COMMAND_TEXTURE_RECTANGLE = 0xE4;
constexpr uint8_t COMMAND_TEXTURE_RECTANGLE_FLIP = 0xE5;
constexpr uint8_t COMMAND_LOAD_SYNC = 0xE6;
constexpr uint8_t COMMAND_PIPE_SYNC = 0xE7;
constexpr uint8_t COMMAND_TILE_SYNC = 0xE8;
constexpr uint8_t COMMAND_FULL_SYNC = 0xE9;
constexpr uint8_t COMMAND_SET_KEY_GB = 0xEA;
constexpr uint8_t COMMAND_SET_KEY_R = 0xEB;
constexpr uint8_t COMMAND_SET_CONVERT = 0xEC;
constexpr uint8_t COMMAND_SET_SCISSOR = 0xED;
constexpr uint8_t COMMAND_SET_PRIMITIVE_DEPTH = 0xEE;
constexpr uint8_t COMMAND_RDP_SET_OTHER_MODE = 0xEF;
constexpr uint8_t COMMAND_FILL_RECTANGLE = 0xF6;
constexpr uint8_t COMMAND_SET_FILL_COLOR = 0xF7;
constexpr uint8_t COMMAND_SET_FOG_COLOR = 0xF8;
constexpr uint8_t COMMAND_SET_BLEND_COLOR = 0xF9;
constexpr uint8_t COMMAND_SET_PRIMITIVE_COLOR = 0xFA;
constexpr uint8_t COMMAND_SET_ENVIRONMENT_COLOR = 0xFB;
constexpr uint8_t COMMAND_SET_DEPTH_IMAGE = 0xFE;
constexpr uint8_t COMMAND_SET_COLOR_IMAGE = 0xFF;

// F3DEX2/GBI2 command bytes. Field extraction follows the MIT-licensed
// LibUltraShip interpreter and its vendored libultra gbi.h macros. Keep this
// table separate: several bytes mean different operations in older dialects.
constexpr uint8_t F3DEX2_COMMAND_NO_OP = 0x00;
constexpr uint8_t F3DEX2_COMMAND_VERTEX = 0x01;
constexpr uint8_t F3DEX2_COMMAND_MODIFY_VERTEX = 0x02;
constexpr uint8_t F3DEX2_COMMAND_TRIANGLE_1 = 0x05;
constexpr uint8_t F3DEX2_COMMAND_TRIANGLE_2 = 0x06;
constexpr uint8_t F3DEX2_COMMAND_QUAD = 0x07;
constexpr uint8_t F3DEX2_COMMAND_TEXTURE = 0xD7;
constexpr uint8_t F3DEX2_COMMAND_POP_MATRIX = 0xD8;
constexpr uint8_t F3DEX2_COMMAND_GEOMETRY_MODE = 0xD9;
constexpr uint8_t F3DEX2_COMMAND_MATRIX = 0xDA;
constexpr uint8_t F3DEX2_COMMAND_MOVE_WORD = 0xDB;
constexpr uint8_t F3DEX2_COMMAND_MOVE_MEMORY = 0xDC;
constexpr uint8_t F3DEX2_COMMAND_DISPLAY_LIST = 0xDE;
constexpr uint8_t F3DEX2_COMMAND_END_DISPLAY_LIST = 0xDF;
constexpr uint8_t F3DEX2_COMMAND_SP_NO_OP = 0xE0;
constexpr uint8_t F3DEX2_COMMAND_RDP_HALF_1 = 0xE1;
constexpr uint8_t F3DEX2_COMMAND_SET_OTHER_MODE_L = 0xE2;
constexpr uint8_t F3DEX2_COMMAND_SET_OTHER_MODE_H = 0xE3;
constexpr uint8_t F3DEX2_COMMAND_RDP_HALF_2 = 0xF1;
constexpr uint8_t F3DEX2_MOVE_MEMORY_VIEWPORT = 0x08;
constexpr uint8_t F3DEX2_MODIFY_VERTEX_RGBA = 0x10;
constexpr uint8_t F3DEX2_MODIFY_VERTEX_ST = 0x14;
constexpr uint8_t F3DEX2_MODIFY_VERTEX_XYSCREEN = 0x18;
constexpr uint8_t F3DEX2_MODIFY_VERTEX_ZSCREEN = 0x1C;
constexpr uint32_t F3DEX2_GEOMETRY_MODE_CULL_FRONT = 0x00000200U;
constexpr uint32_t F3DEX2_GEOMETRY_MODE_CULL_BACK = 0x00000400U;

constexpr uint8_t MATRIX_PROJECTION = 0x01U;
constexpr uint8_t MATRIX_LOAD = 0x02U;
constexpr uint8_t MATRIX_PUSH = 0x04U;
constexpr uint8_t F3DEX2_MATRIX_PROJECTION = 0x04U;
constexpr uint8_t F3DEX2_MATRIX_PUSH = 0x01U;
constexpr uint8_t MOVE_MEMORY_VIEWPORT = 0x80U;
constexpr uint32_t GEOMETRY_MODE_ZBUFFER = 0x00000001U;
constexpr uint32_t GEOMETRY_MODE_CULL_FRONT = 0x00001000U;
constexpr uint32_t GEOMETRY_MODE_CULL_BACK = 0x00002000U;
constexpr uint32_t GEOMETRY_MODE_LIGHTING = 0x00020000U;
constexpr uint32_t OTHER_MODE_Z_COMPARE = 0x00000010U;
constexpr uint32_t OTHER_MODE_Z_UPDATE = 0x00000020U;
constexpr uint32_t OTHER_MODE_COVERAGE_DESTINATION = 0x00000300U;
constexpr uint32_t OTHER_MODE_COVERAGE_TIMES_ALPHA = 0x00001000U;
constexpr uint32_t OTHER_MODE_ALPHA_COVERAGE_SELECT = 0x00002000U;
constexpr uint32_t OTHER_MODE_FORCE_BLEND = 0x00004000U;
constexpr uint32_t OTHER_MODE_ALPHA_COMPARE = 0x00000003U;
constexpr uint32_t OTHER_MODE_HIGH_CYCLE_TYPE = 0x00300000U;
constexpr uint32_t IMAGE_FORMAT_RGBA = 0;
constexpr uint32_t IMAGE_FORMAT_CI = 2;
constexpr uint32_t IMAGE_FORMAT_IA = 3;
constexpr uint32_t IMAGE_FORMAT_I = 4;
constexpr uint32_t IMAGE_SIZE_4_BIT = 0;
constexpr uint32_t IMAGE_SIZE_8_BIT = 1;
constexpr uint32_t IMAGE_SIZE_16_BIT = 2;
constexpr uint32_t IMAGE_SIZE_32_BIT = 3;
constexpr size_t TMEM_BYTE_COUNT = 4096;
constexpr size_t N64_VERTEX_SIZE = 16;
constexpr size_t MAX_DISPLAY_LIST_DEPTH = 32;
constexpr size_t MAX_COMMANDS = 100000;
constexpr size_t MAX_MODELVIEW_STACK_DEPTH = 32;

struct DecodedVertex {
    int16_t position_x = 0;
    int16_t position_y = 0;
    int16_t position_z = 0;
    int16_t texture_s = 0;
    int16_t texture_t = 0;
    uint8_t attribute_x = 0;
    uint8_t attribute_y = 0;
    uint8_t attribute_z = 0;
    uint8_t alpha = 0xFF;
    bool uses_lighting = true;
    bool has_screen_xy_override = false;
    int16_t screen_x_s13_2 = 0;
    int16_t screen_y_s13_2 = 0;
    bool has_screen_z_override = false;
    uint32_t screen_z = 0;
    uint32_t modelview_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t projection_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t viewport_index = FAST3D_CAPTURE_NO_INDEX;
};

struct TextureState {
    uint32_t address = 0;
    uint32_t format = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t active_tile = 0;
    uint32_t tmem_generation = 0;
    bool clamp_s = false;
    bool clamp_t = false;
};

struct TileState {
    bool configured = false;
    uint32_t format = 0;
    uint32_t size = 0;
    uint32_t line = 0;
    uint32_t tmem_word_offset = 0;
    uint32_t palette = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool clamp_s = false;
    bool clamp_t = false;
};

uint32_t read_big_endian_u32(const std::vector<uint8_t> &data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24U) |
            (static_cast<uint32_t>(data[offset + 1]) << 16U) |
            (static_cast<uint32_t>(data[offset + 2]) << 8U) |
            static_cast<uint32_t>(data[offset + 3]);
}

int16_t read_big_endian_i16(const std::vector<uint8_t> &data, size_t offset) {
    const uint16_t value =
            (static_cast<uint16_t>(data[offset]) << 8U) |
            static_cast<uint16_t>(data[offset + 1]);
    return static_cast<int16_t>(value);
}

bool resolve_segment_address(
        const N64SegmentMap &segments,
        uint32_t segmented_address,
        size_t required_size,
        const std::vector<uint8_t> *&segment,
        size_t &offset,
        std::string &error) {
    const uint32_t region = segmented_address & 0xe0000000U;
    if (region == 0x80000000U || region == 0xa0000000U) {
        // F3DEX can use direct KSEG0/KSEG1 RDRAM pointers in display lists.
        segment = segments.get(0U);
        offset = segmented_address & 0x1fffffffU;
    } else {
        const uint8_t segment_id = static_cast<uint8_t>(
                segmented_address >> 24U);
        segment = segments.get(segment_id);
        offset = segmented_address & 0x00FFFFFFU;
    }
    if (segment == nullptr) {
        error = "A GBI command referenced an unloaded ROM segment.";
        return false;
    }
    if (offset > segment->size() || required_size > segment->size() - offset) {
        error = "A GBI command referenced data outside its captured segment (address=" + std::to_string(segmented_address) + ", offset=" + std::to_string(offset) + ", required=" + std::to_string(required_size) + ", size=" + std::to_string(segment->size()) + ").";
        return false;
    }
    return true;
}

std::vector<uint8_t> decode_rgba16(const uint8_t *source, size_t pixel_count) {
    std::vector<uint8_t> rgba(pixel_count * 4);
    for (size_t index = 0; index < pixel_count; ++index) {
        const uint16_t value =
                (static_cast<uint16_t>(source[index * 2]) << 8U) |
                source[index * 2 + 1];
        const uint8_t red = static_cast<uint8_t>((value >> 11U) & 0x1F);
        const uint8_t green = static_cast<uint8_t>((value >> 6U) & 0x1F);
        const uint8_t blue = static_cast<uint8_t>((value >> 1U) & 0x1F);
        rgba[index * 4] = static_cast<uint8_t>((red << 3U) | (red >> 2U));
        rgba[index * 4 + 1] = static_cast<uint8_t>((green << 3U) | (green >> 2U));
        rgba[index * 4 + 2] = static_cast<uint8_t>((blue << 3U) | (blue >> 2U));
        rgba[index * 4 + 3] = (value & 1U) != 0 ? 0xFF : 0x00;
    }
    return rgba;
}

std::vector<uint8_t> decode_ia16(const uint8_t *source, size_t pixel_count) {
    std::vector<uint8_t> rgba(pixel_count * 4);
    for (size_t index = 0; index < pixel_count; ++index) {
        const uint8_t intensity = source[index * 2];
        const uint8_t alpha = source[index * 2 + 1];
        rgba[index * 4] = intensity;
        rgba[index * 4 + 1] = intensity;
        rgba[index * 4 + 2] = intensity;
        rgba[index * 4 + 3] = alpha;
    }
    return rgba;
}

class N64GbiDecoder {
public:
    N64GbiDecoder(
            N64MicrocodeFamily p_microcode_family,
            const N64SegmentMap &p_segments) :
            microcode_family(p_microcode_family), segments(p_segments) {
    }

    bool decode(
            const std::vector<DisplayListRoot> &roots,
            RenderSceneData &scene,
            std::string &error) {
        if (!supports_dialect()) {
            error = std::string("XR64 has no explicit ") + dialect_name() +
                    " scene decoder.";
            return false;
        }
        if (roots.empty()) {
            error = "No GBI display-list roots were provided.";
            return false;
        }
        output = &scene;
        output->textures.clear();
        output->surfaces.clear();
        output->fast3d = {};
        initialize_command_coverage();
        output->fast3d.provenance =
                Fast3DStateProvenance::StaticSegmentData;
        texture_keys.clear();
        texture_key_indices.clear();
        command_count = 0;
        reset_texture_state();
        reset_semantic_state();
        reset_segment_state();

        for (const DisplayListRoot &root : roots) {
            current_root = root;
            if (!decode_display_list(root.segmented_address, 0, error)) {
                return false;
            }
        }
        if (output->textures.empty() || output->surfaces.empty()) {
            error = "GBI decoding produced no renderable scene data.";
            return false;
        }
        output->flat_reference_texture_index = 0;
        return true;
    }

    bool decode_actor_parts(
            const std::vector<ActorDisplayListRoot> &roots,
            std::vector<RenderSceneData> &part_models,
            std::string &error) {
        if (!supports_dialect()) {
            error = std::string("XR64 has no explicit ") + dialect_name() +
                    " actor decoder.";
            return false;
        }
        if (roots.empty() || part_models.empty()) {
            error = "No GBI actor-part roots were provided.";
            return false;
        }

        for (RenderSceneData &model : part_models) {
            model.textures.clear();
            model.surfaces.clear();
            model.flat_reference_texture_index = 0;
            model.fast3d = {};
            model.fast3d.microcode_family = microcode_family;
            model.fast3d.provenance =
                    Fast3DStateProvenance::StaticSegmentData;
        }
        std::vector<bool> decoded_parts(part_models.size(), false);
        command_count = 0;

        for (const ActorDisplayListRoot &root : roots) {
            if (root.part_index >= part_models.size() ||
                    decoded_parts[root.part_index]) {
                error = "GBI received an invalid actor-part root.";
                return false;
            }
            decoded_parts[root.part_index] = true;
            output = &part_models[root.part_index];
            current_root = root.display_list;
            texture_keys.clear();
        texture_key_indices.clear();
            vertex_loaded.fill(false);
            reset_semantic_state();
            reset_segment_state();
            if (!decode_display_list(
                        root.display_list.segmented_address,
                        0,
                        error)) {
                return false;
            }
            if (output->surfaces.empty() != output->textures.empty()) {
                error = "GBI produced an incomplete actor-part model.";
                return false;
            }
            output->flat_reference_texture_index = 0;
        }
        return true;
    }

private:
    N64MicrocodeFamily microcode_family = N64MicrocodeFamily::Unknown;
    const N64SegmentMap &segments;
    RenderSceneData *output = nullptr;
    DisplayListRoot current_root;
    TextureState texture_state;
    std::array<TileState, 8> tile_states{};
    std::array<uint8_t, TMEM_BYTE_COUNT> tmem{};
    std::array<uint8_t, TMEM_BYTE_COUNT> tmem_valid{};
    uint32_t active_tile = 0;
    uint32_t tmem_generation = 0;
    uint32_t tlut_tmem_byte_offset = FAST3D_CAPTURE_NO_INDEX;
    RdpCombinerState rdp_combiner;
    bool other_mode_low_captured = false;
    uint32_t other_mode_low = 0;
    bool other_mode_high_captured = false;
    uint32_t other_mode_high = 0;
    bool texture_enabled = false;
    float texture_scale_s = 1.0F;
    float texture_scale_t = 1.0F;
    uint32_t geometry_mode = GEOMETRY_MODE_LIGHTING;
    std::array<DecodedVertex, 64> vertex_cache{};
    std::array<bool, 64> vertex_loaded{};
    uint32_t triangle_index_divisor = 10U;
    std::vector<TextureState> texture_keys;
    std::vector<uint32_t> texture_key_indices;
    size_t command_count = 0;
    float light_red = 1.0F;
    float light_green = 1.0F;
    float light_blue = 1.0F;
    uint32_t modelview_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t projection_matrix_index = FAST3D_CAPTURE_NO_INDEX;
    uint32_t viewport_index = FAST3D_CAPTURE_NO_INDEX;
    std::vector<uint32_t> modelview_stack;

    bool use_live_rdram = false;
    std::array<uint32_t, 32> live_segment_bases{};
    std::array<bool, 32> live_segment_loaded{};

    bool supports_dialect() const {
        return microcode_family == N64MicrocodeFamily::Fast3D ||
                microcode_family == N64MicrocodeFamily::F3DEX ||
                microcode_family == N64MicrocodeFamily::F3DEX2;
    }

    const char *dialect_name() const {
        switch (microcode_family) {
            case N64MicrocodeFamily::Fast3D: return "Fast3D";
            case N64MicrocodeFamily::F3DEX: return "F3DEX";
            case N64MicrocodeFamily::F3DEX2: return "F3DEX2";
            case N64MicrocodeFamily::S2DEX: return "S2DEX";
            case N64MicrocodeFamily::Unknown: return "Unknown GBI";
        }
        return "Unknown GBI";
    }

    void initialize_command_coverage() {
        output->fast3d.microcode_family = microcode_family;
        output->fast3d.command_count = 0;
        output->fast3d.no_op_command_count = 0;
        output->fast3d.passthrough_command_count = 0;
        output->fast3d.passthrough_opcodes.clear();
    }

    void record_no_op_command() {
        ++output->fast3d.no_op_command_count;
    }

    void record_passthrough_command(uint8_t opcode) {
        ++output->fast3d.passthrough_command_count;
        if (std::find(output->fast3d.passthrough_opcodes.begin(),
                    output->fast3d.passthrough_opcodes.end(), opcode) ==
                output->fast3d.passthrough_opcodes.end()) {
            output->fast3d.passthrough_opcodes.push_back(opcode);
        }
    }

    bool unsupported_opcode(
            uint8_t opcode,
            uint32_t source_command_address,
            std::string &error) const {
        char detail[160]{};
        std::snprintf(detail, sizeof(detail),
                "%s decoder has no explicit handler for opcode 0x%02X at 0x%08X.",
                dialect_name(), opcode, source_command_address);
        error = detail;
        return false;
    }

    bool valid_other_mode_field(uint32_t word0) const {
        uint32_t shift = (word0 >> 8U) & 0xFFU;
        uint32_t length = word0 & 0xFFU;
        if (microcode_family == N64MicrocodeFamily::F3DEX2) {
            length += 1U;
            if (shift + length > 32U) return false;
            shift = 32U - shift - length;
        }
        return shift < 32U && length <= 32U - shift;
    }

    uint32_t cull_front_mask() const {
        return microcode_family == N64MicrocodeFamily::F3DEX2 ?
                F3DEX2_GEOMETRY_MODE_CULL_FRONT : GEOMETRY_MODE_CULL_FRONT;
    }

    uint32_t cull_back_mask() const {
        return microcode_family == N64MicrocodeFamily::F3DEX2 ?
                F3DEX2_GEOMETRY_MODE_CULL_BACK : GEOMETRY_MODE_CULL_BACK;
    }

    static bool is_segment_move_word(uint32_t word0) {
        return (word0 & 0xffU) == MOVE_WORD_SEGMENT ||
                static_cast<uint8_t>(word0 >> 16U) == MOVE_WORD_SEGMENT;
    }

    void reset_segment_state() {
        use_live_rdram = !segments.live_rdram.empty();
        live_segment_bases = segments.live_segment_bases;
        live_segment_loaded = segments.live_segment_loaded;
        live_segment_bases[0] = 0U;
        live_segment_loaded[0] = true;
    }

    bool resolve_current_segment_address(
            uint32_t segmented_address,
            size_t required_size,
            const std::vector<uint8_t> *&segment,
            size_t &offset,
            std::string &error) const {
        if (!use_live_rdram) {
            return resolve_segment_address(
                    segments, segmented_address, required_size,
                    segment, offset, error);
        }
        uint64_t physical = 0U;
        const uint32_t region = segmented_address & 0xe0000000U;
        if (region == 0x80000000U || region == 0xa0000000U) {
            physical = segmented_address & 0x1fffffffU;
        } else {
            const uint8_t segment_id =
                    static_cast<uint8_t>(segmented_address >> 24U);
            if (segment_id >= live_segment_loaded.size() ||
                    !live_segment_loaded[segment_id]) {
                error = "Live GBI referenced an unset segment.";
                return false;
            }
            physical = static_cast<uint64_t>(live_segment_bases[segment_id]) +
                    (segmented_address & 0x00ffffffU);
        }
        if (physical > segments.live_rdram.size() ||
                required_size > segments.live_rdram.size() - physical) {
            error = "Live GBI reference is outside task-start RDRAM.";
            return false;
        }
        segment = &segments.live_rdram;
        offset = static_cast<size_t>(physical);
        return true;
    }

    bool apply_move_word_segment(
            uint32_t word0, uint32_t word1, std::string &error) {
        if (!use_live_rdram ||
                ((word0 & 0xffU) != MOVE_WORD_SEGMENT &&
                 static_cast<uint8_t>(word0 >> 16U) !=
                         MOVE_WORD_SEGMENT)) {
            return true;
        }
        const uint32_t segment_index = (word0 & 0xffU) ==
                        MOVE_WORD_SEGMENT ?
                (((word0 >> 8U) & 0xffffU) >> 2U) :
                ((word0 & 0xffffU) >> 2U);
        const uint32_t base = word1 & 0x00ffffffU;
        if (segment_index >= live_segment_loaded.size() ||
                base >= segments.live_rdram.size()) {
            error = "Live GBI set an invalid segment base.";
            return false;
        }
        live_segment_bases[segment_index] = base;
        live_segment_loaded[segment_index] = true;
        return true;
    }


    void reset_texture_state() {
        texture_state = {};
        tile_states = {};
        tmem = {};
        tmem_valid = {};
        active_tile = 0;
        tmem_generation = 0;
        tlut_tmem_byte_offset = FAST3D_CAPTURE_NO_INDEX;
        rdp_combiner = {};
        other_mode_low_captured = false;
        other_mode_low = 0;
        other_mode_high_captured = false;
        other_mode_high = 0;
        texture_enabled = false;
        texture_scale_s = 1.0F;
        texture_scale_t = 1.0F;
    }

    void reset_semantic_state() {
        triangle_index_divisor = (microcode_family ==
                        N64MicrocodeFamily::F3DEX ||
                microcode_family == N64MicrocodeFamily::F3DEX2) ?
                2U : 10U;
        modelview_matrix_index = FAST3D_CAPTURE_NO_INDEX;
        projection_matrix_index = FAST3D_CAPTURE_NO_INDEX;
        viewport_index = FAST3D_CAPTURE_NO_INDEX;
        modelview_stack.clear();
    }

    static uint32_t command_address(uint32_t segmented_address, size_t offset) {
        return (segmented_address & 0xff000000U) |
                static_cast<uint32_t>(offset & 0x00ffffffU);
    }

    bool capture_matrix(
            uint32_t word0,
            uint32_t address,
            uint32_t source_command_address,
            std::string &error) {
        uint8_t parameters = 0U;
        if (microcode_family == N64MicrocodeFamily::F3DEX2) {
            if (((word0 >> 19U) & 0x1FU) != 7U ||
                    ((word0 >> 8U) & 0xFFU) != 0U) {
                error = "F3DEX2 G_MTX has an unsupported DMA length or offset.";
                return false;
            }
            parameters = static_cast<uint8_t>(word0 & 0xFFU) ^ F3DEX2_MATRIX_PUSH;
        } else {
            if ((word0 & 0xffffU) != 64U) {
                error = "GBI matrix command has an unsupported byte count.";
                return false;
            }
            parameters = static_cast<uint8_t>((word0 >> 16U) & 0xffU);
        }
        const std::vector<uint8_t> *segment = nullptr;
        size_t offset = 0;
        if (!resolve_current_segment_address(
                    address, 64U, segment, offset, error)) {
            return false;
        }

        const uint8_t projection_mask =
                microcode_family == N64MicrocodeFamily::F3DEX2 ?
                F3DEX2_MATRIX_PROJECTION : MATRIX_PROJECTION;
        const uint8_t push_mask = microcode_family == N64MicrocodeFamily::F3DEX2 ?
                F3DEX2_MATRIX_PUSH : MATRIX_PUSH;
        const bool projection = (parameters & projection_mask) != 0;
        const bool load = (parameters & MATRIX_LOAD) != 0;
        const bool push = (parameters & push_mask) != 0;
        Fast3DMatrixCapture capture;
        capture.command_address = source_command_address;
        capture.matrix_address = address;
        capture.parameters = parameters;
        capture.projection = projection;
        capture.load = load;
        capture.push = push;
        capture.previous_matrix_index = projection ? projection_matrix_index :
                modelview_matrix_index;
        capture.modelview_stack_depth =
                static_cast<uint32_t>(modelview_stack.size());
        for (size_t index = 0; index < capture.fixed_16_16.size(); ++index) {
            const int16_t integer =
                    read_big_endian_i16(*segment, offset + index * 2U);
            const uint16_t fraction = static_cast<uint16_t>(
                    (static_cast<uint16_t>(
                            (*segment)[offset + 32U + index * 2U]) << 8U) |
                    (*segment)[offset + 32U + index * 2U + 1U]);
            capture.fixed_16_16[index] =
                    static_cast<int32_t>(integer) * 65536 + fraction;
        }
        const uint32_t capture_index =
                static_cast<uint32_t>(output->fast3d.matrices.size());
        output->fast3d.matrices.push_back(capture);
        output->fast3d.matrix_confidence = Fast3DStateConfidence::Captured;

        if (projection) {
            projection_matrix_index = capture_index;
            return true;
        }
        if (push) {
            if (modelview_stack.size() >= MAX_MODELVIEW_STACK_DEPTH) {
                error = "GBI exceeded XR64's captured model-view stack limit.";
                return false;
            }
            modelview_stack.push_back(modelview_matrix_index);
        }
        modelview_matrix_index = capture_index;
        return true;
    }

    bool capture_viewport(
            uint32_t word0,
            uint32_t address,
            uint32_t source_command_address,
            std::string &error) {
        if (microcode_family == N64MicrocodeFamily::F3DEX2) {
            if ((word0 & 0xFFU) != F3DEX2_MOVE_MEMORY_VIEWPORT) return true;
            if (((word0 >> 19U) & 0x1FU) != 1U ||
                    ((word0 >> 8U) & 0xFFU) != 0U) {
                error = "F3DEX2 G_MOVEMEM viewport has an unsupported DMA length or offset.";
                return false;
            }
        } else {
            if (((word0 >> 16U) & 0xffU) != MOVE_MEMORY_VIEWPORT) return true;
            if ((word0 & 0xffffU) != 16U) {
                error = "GBI viewport command has an unsupported byte count.";
                return false;
            }
        }
        const std::vector<uint8_t> *segment = nullptr;
        size_t offset = 0;
        if (!resolve_current_segment_address(
                    address, 16U, segment, offset, error)) {
            return false;
        }
        Fast3DViewportCapture capture;
        capture.command_address = source_command_address;
        capture.viewport_address = address;
        for (size_t index = 0; index < 4U; ++index) {
            capture.scale[index] =
                    read_big_endian_i16(*segment, offset + index * 2U);
            capture.translate[index] = read_big_endian_i16(
                    *segment, offset + 8U + index * 2U);
        }
        viewport_index =
                static_cast<uint32_t>(output->fast3d.viewports.size());
        output->fast3d.viewports.push_back(capture);
        output->fast3d.viewport_confidence = Fast3DStateConfidence::Captured;
        return true;
    }

    void pop_modelview_matrix() {
        if (modelview_stack.empty()) {
            // The task may inherit RSP state from before this bounded capture.
            // Mark it unavailable rather than guessing.
            modelview_matrix_index = FAST3D_CAPTURE_NO_INDEX;
            return;
        }
        modelview_matrix_index = modelview_stack.back();
        modelview_stack.pop_back();
    }

    uint32_t capture_draw_state(uint32_t source_command_address) {
        Fast3DDrawStateCapture capture;
        capture.command_address = source_command_address;
        capture.modelview_matrix_index = modelview_matrix_index;
        capture.projection_matrix_index = projection_matrix_index;
        capture.viewport_index = viewport_index;
        capture.modelview_stack_depth =
                static_cast<uint32_t>(modelview_stack.size());
        const uint32_t draw_index =
                static_cast<uint32_t>(output->fast3d.draws.size());
        output->fast3d.draws.push_back(capture);
        output->fast3d.draw_state_confidence = Fast3DStateConfidence::Captured;
        return draw_index;
    }


    bool load_vertices(uint32_t word0, uint32_t address, std::string &error) {
        uint32_t count = 0U;
        uint32_t first = 0U;
        if (microcode_family == N64MicrocodeFamily::F3DEX2) {
            count = (word0 >> 12U) & 0xFFU;
            const uint32_t end_times_two = (word0 >> 1U) & 0x7FU;
            if (count == 0U || end_times_two < count) {
                error = "F3DEX2 G_VTX has an invalid count or v0+n field.";
                return false;
            }
            first = end_times_two - count;
        } else if (microcode_family == N64MicrocodeFamily::F3DEX) {
            count = (word0 >> 10U) & 0x3FU;
            const uint32_t encoded_length = word0 & 0x03FFU;
            const uint32_t first_times_two = (word0 >> 16U) & 0xFFU;
            if (count == 0U ||
                    encoded_length != count * N64_VERTEX_SIZE - 1U ||
                    (first_times_two & 1U) != 0U) {
                error = "F3DEX G_VTX has an invalid count, length, or v0 field.";
                return false;
            }
            first = first_times_two / 2U;
        } else {
            const uint32_t encoded_length = word0 & 0xFFFFU;
            if (encoded_length == 0U ||
                    encoded_length % N64_VERTEX_SIZE != 0U) {
                error = "GBI G_VTX has an invalid byte length.";
                return false;
            }
            count = encoded_length / N64_VERTEX_SIZE;
            first = (word0 >> 16U) & 0x0FU;
        }
        if (count == 0 || first >= vertex_cache.size() ||
                count > vertex_cache.size() - first) {
            error = std::string(dialect_name()) +
                    " requested an invalid vertex-cache range.";
            return false;
        }

        const std::vector<uint8_t> *segment = nullptr;
        size_t offset = 0;
        if (!resolve_current_segment_address(
                    address,
                    static_cast<size_t>(count) * N64_VERTEX_SIZE,
                    segment,
                    offset,
                    error)) {
            return false;
        }
        const bool lighting = (geometry_mode & GEOMETRY_MODE_LIGHTING) != 0;
        for (uint32_t index = 0; index < count; ++index) {
            const size_t vertex_offset = offset + index * N64_VERTEX_SIZE;
            DecodedVertex &vertex = vertex_cache[first + index];
            vertex.position_x = read_big_endian_i16(*segment, vertex_offset);
            vertex.position_y = read_big_endian_i16(*segment, vertex_offset + 2);
            vertex.position_z = read_big_endian_i16(*segment, vertex_offset + 4);
            vertex.texture_s = read_big_endian_i16(*segment, vertex_offset + 8);
            vertex.texture_t = read_big_endian_i16(*segment, vertex_offset + 10);
            vertex.attribute_x = (*segment)[vertex_offset + 12];
            vertex.attribute_y = (*segment)[vertex_offset + 13];
            vertex.attribute_z = (*segment)[vertex_offset + 14];
            vertex.alpha = (*segment)[vertex_offset + 15];
            vertex.uses_lighting = lighting;
            vertex.modelview_matrix_index = modelview_matrix_index;
            vertex.projection_matrix_index = projection_matrix_index;
            vertex.viewport_index = viewport_index;
            vertex_loaded[first + index] = true;
        }
        return true;
    }

    bool modify_vertex(uint32_t word0, uint32_t word1, std::string &error) {
        const uint32_t encoded_index = word0 & 0xFFFFU;
        const uint8_t where = static_cast<uint8_t>((word0 >> 16U) & 0xFFU);
        if ((encoded_index & 1U) != 0U) {
            error = "F3DEX2 G_MODIFYVTX has an unaligned vertex index.";
            return false;
        }
        const uint32_t index = encoded_index >> 1U;
        if (index >= vertex_cache.size() || !vertex_loaded[index]) {
            error = "F3DEX2 G_MODIFYVTX referenced an unloaded vertex.";
            return false;
        }
        DecodedVertex &vertex = vertex_cache[index];
        switch (where) {
            case F3DEX2_MODIFY_VERTEX_RGBA:
                // Nintendo's GBI contract says this is the final lit color,
                // not a replacement normal that should be lit again.
                vertex.attribute_x = static_cast<uint8_t>(word1 >> 24U);
                vertex.attribute_y = static_cast<uint8_t>(word1 >> 16U);
                vertex.attribute_z = static_cast<uint8_t>(word1 >> 8U);
                vertex.alpha = static_cast<uint8_t>(word1);
                vertex.uses_lighting = false;
                return true;
            case F3DEX2_MODIFY_VERTEX_ST:
                vertex.texture_s = static_cast<int16_t>(word1 >> 16U);
                vertex.texture_t = static_cast<int16_t>(word1 & 0xFFFFU);
                return true;
            case F3DEX2_MODIFY_VERTEX_XYSCREEN:
                vertex.has_screen_xy_override = true;
                vertex.screen_x_s13_2 = static_cast<int16_t>(word1 >> 16U);
                vertex.screen_y_s13_2 = static_cast<int16_t>(word1 & 0xFFFFU);
                return true;
            case F3DEX2_MODIFY_VERTEX_ZSCREEN:
                vertex.has_screen_z_override = true;
                vertex.screen_z = word1;
                return true;
            default:
                char detail[96]{};
                std::snprintf(detail, sizeof(detail),
                        "F3DEX2 G_MODIFYVTX field 0x%02X is unsupported.", where);
                error = detail;
                return false;
        }
    }

    static uint32_t texel_bit_count(uint32_t size) {
        switch (size) {
            case IMAGE_SIZE_4_BIT: return 4;
            case IMAGE_SIZE_8_BIT: return 8;
            case IMAGE_SIZE_16_BIT: return 16;
            case IMAGE_SIZE_32_BIT: return 32;
            default: return 0;
        }
    }

    bool copy_texture_image_to_tmem(
            uint32_t tile_index,
            uint32_t source_texel_offset,
            uint32_t texel_count,
            bool is_tlut,
            std::string &error) {
        if (tile_index >= tile_states.size() || texture_state.address == 0 ||
                texel_count == 0) {
            error = "GBI texture load has no supported image source.";
            return false;
        }
        const uint32_t bits_per_texel = texel_bit_count(texture_state.size);
        if (bits_per_texel == 0U) {
            error = "GBI texture load has an unsupported texel size.";
            return false;
        }
        const uint64_t source_bit_offset =
                static_cast<uint64_t>(source_texel_offset) * bits_per_texel;
        const uint64_t byte_count =
                (static_cast<uint64_t>(texel_count) * bits_per_texel + 7U) / 8U;
        const uint64_t source_byte_offset = source_bit_offset / 8U;
        const uint64_t source_byte_count =
                (source_bit_offset % 8U +
                        static_cast<uint64_t>(texel_count) * bits_per_texel + 7U) / 8U;
        const TileState &tile = tile_states[tile_index];
        const uint64_t destination = static_cast<uint64_t>(tile.tmem_word_offset) * 8U;
        if (!tile.configured || byte_count == 0 ||
                destination + byte_count > TMEM_BYTE_COUNT) {
            error = "GBI texture load exceeds bounded TMEM.";
            return false;
        }
        const std::vector<uint8_t> *segment = nullptr;
        size_t image_offset = 0;
        if (!resolve_current_segment_address(texture_state.address,
                    static_cast<size_t>(source_byte_offset + source_byte_count),
                    segment, image_offset, error)) {
            return false;
        }
        if (source_bit_offset % 8U == 0U) {
            std::copy_n(segment->data() + image_offset + source_byte_offset,
                    static_cast<size_t>(byte_count), tmem.data() + destination);
        } else if (bits_per_texel == 4U) {
            // RDP permits a CI4/IA4 source rectangle to begin on an odd texel.
            // Repack that half-byte-offset source into the byte-aligned TMEM tile.
            std::fill_n(tmem.data() + destination, static_cast<size_t>(byte_count), static_cast<uint8_t>(0U));
            for (uint32_t texel = 0U; texel < texel_count; ++texel) {
                const uint64_t source_bit = source_bit_offset + texel * 4U;
                const uint8_t source_byte = segment->at(static_cast<size_t>(
                        image_offset + source_bit / 8U));
                const uint8_t nibble = (source_bit % 8U) == 0U ?
                        static_cast<uint8_t>(source_byte >> 4U) :
                        static_cast<uint8_t>(source_byte & 0x0FU);
                uint8_t &destination_byte = tmem[destination + texel / 2U];
                if ((texel & 1U) == 0U) {
                    destination_byte = static_cast<uint8_t>(nibble << 4U);
                } else {
                    destination_byte = static_cast<uint8_t>(destination_byte | nibble);
                }
            }
        } else {
            error = "GBI texture load has an unsupported sub-byte offset.";
            return false;
        }
        std::fill_n(tmem_valid.data() + destination,
                static_cast<size_t>(byte_count), static_cast<uint8_t>(1));
        ++tmem_generation;
        if (is_tlut) {
            tlut_tmem_byte_offset = static_cast<uint32_t>(destination);
        }
        return true;
    }

    bool decode_tmem_texture(
            const TileState &tile,
            ModelTexture &texture,
            std::string &error) const {
        const uint32_t bits_per_texel = texel_bit_count(tile.size);
        if (!tile.configured || tile.width == 0 || tile.height == 0 ||
                bits_per_texel == 0 ||
                (tile.format != IMAGE_FORMAT_RGBA && tile.format != IMAGE_FORMAT_IA &&
                        tile.format != IMAGE_FORMAT_CI && tile.format != IMAGE_FORMAT_I)) {
            error = "GBI selected an unsupported TMEM render tile.";
            return false;
        }
        const uint64_t pixel_count = static_cast<uint64_t>(tile.width) * tile.height;
        const uint64_t packed_row_bytes =
                (static_cast<uint64_t>(tile.width) * bits_per_texel + 7U) / 8U;
        const uint64_t row_bytes = tile.line != 0U ?
                static_cast<uint64_t>(tile.line) * 8U : packed_row_bytes;
        const uint64_t base = static_cast<uint64_t>(tile.tmem_word_offset) * 8U;
        if (pixel_count == 0 || pixel_count > 16U * 1024U * 1024U ||
                base + row_bytes * tile.height > TMEM_BYTE_COUNT) {
            error = "GBI TMEM render tile has invalid dimensions.";
            return false;
        }
        texture.width = tile.width;
        texture.height = tile.height;
        texture.rgba_pixels.resize(static_cast<size_t>(pixel_count) * 4U);
        const auto write_rgba5551 = [&](size_t output_offset, uint16_t value) {
            const uint8_t red = static_cast<uint8_t>((value >> 11U) & 0x1FU);
            const uint8_t green = static_cast<uint8_t>((value >> 6U) & 0x1FU);
            const uint8_t blue = static_cast<uint8_t>((value >> 1U) & 0x1FU);
            texture.rgba_pixels[output_offset] = static_cast<uint8_t>((red << 3U) | (red >> 2U));
            texture.rgba_pixels[output_offset + 1U] = static_cast<uint8_t>((green << 3U) | (green >> 2U));
            texture.rgba_pixels[output_offset + 2U] = static_cast<uint8_t>((blue << 3U) | (blue >> 2U));
            texture.rgba_pixels[output_offset + 3U] = (value & 1U) != 0U ? 0xFFU : 0x00U;
        };
        const auto packed_nibble = [&](uint32_t x, uint32_t byte_index) {
            return static_cast<uint8_t>((x & 1U) == 0U ?
                    (tmem[byte_index] >> 4U) : (tmem[byte_index] & 0x0FU));
        };
        const auto expand_4 = [](uint8_t value) {
            return static_cast<uint8_t>((value << 4U) | value);
        };
        const auto expand_3 = [](uint8_t value) {
            return static_cast<uint8_t>((value << 5U) | (value << 2U) | (value >> 1U));
        };
        for (uint32_t y = 0; y < tile.height; ++y) {
            for (uint32_t x = 0; x < tile.width; ++x) {
                const uint64_t bit = base + y * row_bytes +
                        (static_cast<uint64_t>(x) * bits_per_texel) / 8U;
                const size_t pixel_output = (static_cast<size_t>(y) * tile.width + x) * 4U;
                const uint32_t byte_index = static_cast<uint32_t>(bit);
                const uint32_t bytes_per_texel = (bits_per_texel + 7U) / 8U;
                if (byte_index >= TMEM_BYTE_COUNT ||
                        bytes_per_texel > TMEM_BYTE_COUNT - byte_index) {
                    error = "GBI render tile references unloaded TMEM.";
                    return false;
                }
                for (uint32_t byte = 0U; byte < bytes_per_texel; ++byte) {
                    if (tmem_valid[byte_index + byte] == 0U) {
                        error = "GBI render tile references unloaded TMEM.";
                        return false;
                    }
                }
                if (tile.format == IMAGE_FORMAT_RGBA && bits_per_texel == 32U) {
                    texture.rgba_pixels[pixel_output] = tmem[byte_index];
                    texture.rgba_pixels[pixel_output + 1U] = tmem[byte_index + 1U];
                    texture.rgba_pixels[pixel_output + 2U] = tmem[byte_index + 2U];
                    texture.rgba_pixels[pixel_output + 3U] = tmem[byte_index + 3U];
                } else if (tile.format == IMAGE_FORMAT_RGBA && bits_per_texel == 16U) {
                    write_rgba5551(pixel_output, static_cast<uint16_t>(
                            (static_cast<uint16_t>(tmem[byte_index]) << 8U) |
                            tmem[byte_index + 1U]));
                } else if (tile.format == IMAGE_FORMAT_IA && bits_per_texel == 16U) {
                    texture.rgba_pixels[pixel_output] = tmem[byte_index];
                    texture.rgba_pixels[pixel_output + 1U] = tmem[byte_index];
                    texture.rgba_pixels[pixel_output + 2U] = tmem[byte_index];
                    texture.rgba_pixels[pixel_output + 3U] = tmem[byte_index + 1U];
                } else if (tile.format == IMAGE_FORMAT_IA && bits_per_texel == 8U) {
                    const uint8_t value = tmem[byte_index];
                    const uint8_t intensity = expand_4(static_cast<uint8_t>(value >> 4U));
                    texture.rgba_pixels[pixel_output] = intensity;
                    texture.rgba_pixels[pixel_output + 1U] = intensity;
                    texture.rgba_pixels[pixel_output + 2U] = intensity;
                    texture.rgba_pixels[pixel_output + 3U] = expand_4(
                            static_cast<uint8_t>(value & 0x0FU));
                } else if (tile.format == IMAGE_FORMAT_IA && bits_per_texel == 4U) {
                    const uint8_t value = packed_nibble(x, byte_index);
                    const uint8_t intensity = expand_3(static_cast<uint8_t>(value >> 1U));
                    texture.rgba_pixels[pixel_output] = intensity;
                    texture.rgba_pixels[pixel_output + 1U] = intensity;
                    texture.rgba_pixels[pixel_output + 2U] = intensity;
                    texture.rgba_pixels[pixel_output + 3U] =
                            (value & 1U) != 0U ? 0xFFU : 0x00U;
                } else if (tile.format == IMAGE_FORMAT_I && bits_per_texel == 8U) {
                    const uint8_t intensity = tmem[byte_index];
                    texture.rgba_pixels[pixel_output] = intensity;
                    texture.rgba_pixels[pixel_output + 1U] = intensity;
                    texture.rgba_pixels[pixel_output + 2U] = intensity;
                    texture.rgba_pixels[pixel_output + 3U] = 0xFFU;
                } else if (tile.format == IMAGE_FORMAT_I && bits_per_texel == 4U) {
                    const uint8_t intensity = expand_4(packed_nibble(x, byte_index));
                    texture.rgba_pixels[pixel_output] = intensity;
                    texture.rgba_pixels[pixel_output + 1U] = intensity;
                    texture.rgba_pixels[pixel_output + 2U] = intensity;
                    texture.rgba_pixels[pixel_output + 3U] = 0xFFU;
                } else if (tile.format == IMAGE_FORMAT_CI &&
                        (bits_per_texel == 4U || bits_per_texel == 8U) &&
                        tlut_tmem_byte_offset != FAST3D_CAPTURE_NO_INDEX) {
                    const uint8_t index = bits_per_texel == 4U ?
                            static_cast<uint8_t>((x & 1U) == 0U ?
                                    (tmem[byte_index] >> 4U) :
                                    (tmem[byte_index] & 0x0FU)) : tmem[byte_index];
                    const uint32_t palette_index = bits_per_texel == 4U ?
                            tile.palette * 16U + index : index;
                    const uint32_t palette_byte = tlut_tmem_byte_offset + palette_index * 2U;
                    if (palette_byte + 1U >= TMEM_BYTE_COUNT ||
                            tmem_valid[palette_byte] == 0U ||
                            tmem_valid[palette_byte + 1U] == 0U) {
                        error = "GBI CI render tile references unloaded TLUT.";
                        return false;
                    }
                    write_rgba5551(pixel_output, static_cast<uint16_t>(
                            (static_cast<uint16_t>(tmem[palette_byte]) << 8U) |
                            tmem[palette_byte + 1U]));
                } else {
                    error = "GBI TMEM render tile format is unsupported.";
                    return false;
                }
            }
        }
        return true;
    }

    bool ensure_texture(uint32_t &texture_index, std::string &error) {
        if (!texture_enabled || texture_state.address == 0) {
            for (size_t index = 0; index < texture_keys.size(); ++index) {
                if (texture_keys[index].address == 0) {
                    texture_index = texture_key_indices[index];
                    return true;
                }
            }
            ModelTexture texture;
            texture.width = 1;
            texture.height = 1;
            texture.rgba_pixels = {0xFF, 0xFF, 0xFF, 0xFF};
            output->textures.push_back(std::move(texture));
            TextureState key;
            key.width = 1;
            key.height = 1;
            texture_keys.push_back(key);
            texture_index = static_cast<uint32_t>(output->textures.size() - 1);
            texture_key_indices.push_back(texture_index);
            return true;
        }
        texture_state.active_tile = active_tile;
        texture_state.tmem_generation = tmem_generation;
        if (active_tile >= tile_states.size()) {
            error = "GBI selected an invalid texture tile.";
            return false;
        }
        const TileState &tile = tile_states[active_tile];
        for (size_t index = 0; index < texture_keys.size(); ++index) {
            const TextureState &candidate = texture_keys[index];
            if (candidate.address == texture_state.address &&
                    candidate.active_tile == texture_state.active_tile &&
                    candidate.tmem_generation == texture_state.tmem_generation &&
                    candidate.width == texture_state.width &&
                    candidate.height == texture_state.height) {
                texture_index = texture_key_indices[index];
                return true;
            }
        }

        ModelTexture texture;
        if (tile.configured && tmem_generation != 0U) {
            if (!decode_tmem_texture(tile, texture, error)) return false;
            texture_state.width = tile.width;
            texture_state.height = tile.height;
            texture_state.clamp_s = tile.clamp_s;
            texture_state.clamp_t = tile.clamp_t;
        } else {
            if (texture_state.width == 0 || texture_state.height == 0 ||
                    (texture_state.size != IMAGE_SIZE_16_BIT && texture_state.size != IMAGE_SIZE_32_BIT)) {
                error = "GBI selected a texture without a supported direct source.";
                return false;
            }
            const uint64_t pixel_count =
                    static_cast<uint64_t>(texture_state.width) * texture_state.height;
            if (pixel_count == 0 || pixel_count > 16U * 1024U * 1024U) {
                error = "GBI selected a texture with invalid dimensions.";
                return false;
            }
            const std::vector<uint8_t> *segment = nullptr;
            size_t offset = 0;
            if (!resolve_current_segment_address(texture_state.address,
                        static_cast<size_t>(pixel_count * (texture_state.size == IMAGE_SIZE_32_BIT ? 4U : 2U)), segment, offset, error)) {
                return false;
            }
            texture.width = texture_state.width;
            texture.height = texture_state.height;
            texture.rgba_pixels = texture_state.format == IMAGE_FORMAT_RGBA &&
                    texture_state.size == IMAGE_SIZE_32_BIT ?
                    std::vector<uint8_t>(
                            segment->data() + offset,
                            segment->data() + offset + pixel_count * 4U) :
                    texture_state.format == IMAGE_FORMAT_RGBA ?
                    decode_rgba16(segment->data() + offset, pixel_count) :
                    texture_state.format == IMAGE_FORMAT_IA ?
                            decode_ia16(segment->data() + offset, pixel_count) :
                            std::vector<uint8_t>{};
            if (texture.rgba_pixels.empty()) {
                error = "GBI selected an unsupported direct texture format.";
                return false;
            }
        }
        for (size_t index = 0; index < output->textures.size(); ++index) {
            const ModelTexture &candidate = output->textures[index];
            if (candidate.width == texture.width &&
                    candidate.height == texture.height &&
                    candidate.rgba_pixels == texture.rgba_pixels) {
                texture_keys.push_back(texture_state);
                texture_key_indices.push_back(static_cast<uint32_t>(index));
                texture_index = static_cast<uint32_t>(index);
                return true;
            }
        }
        output->textures.push_back(std::move(texture));
        texture_keys.push_back(texture_state);
        texture_index = static_cast<uint32_t>(output->textures.size() - 1);
        texture_key_indices.push_back(texture_index);
        return true;
    }
    bool ensure_texture_for_tile(
            uint32_t tile_index,
            uint32_t &texture_index,
            std::string &error) {
        if (tile_index >= tile_states.size()) {
            error = "GBI selected an invalid adjacent texture tile.";
            return false;
        }
        const uint32_t saved_active_tile = active_tile;
        const TextureState saved_texture_state = texture_state;
        active_tile = tile_index;
        const bool decoded = ensure_texture(texture_index, error);
        active_tile = saved_active_tile;
        texture_state = saved_texture_state;
        return decoded;
    }

    static RdpCombinerMode classify_combiner(uint32_t word0, uint32_t word1) {
        if (word0 == 0xFC269804U && word1 == 0x1F14FFFFU) {
            return RdpCombinerMode::TrilinearTextureShade;
        }
        if (word0 == 0xFC129804U && word1 == 0x3F15FFFFU) {
            return RdpCombinerMode::PrimitiveEnvironmentTextureShade;
        }
        if (word0 == 0xFC62FE04U && word1 == 0x3F15F9FFU) {
            return RdpCombinerMode::PrimitiveEnvironmentShade;
        }
        const uint32_t color_a = (word0 >> 20U) & 0x0FU;
        const uint32_t color_c = (word0 >> 15U) & 0x1FU;
        const uint32_t color_b = (word1 >> 28U) & 0x0FU;
        const uint32_t color_d = (word1 >> 15U) & 0x07U;
        if (color_b != 0U || color_d != 0U) return RdpCombinerMode::Unsupported;
        if (color_a == 1U && color_c == 4U) {
            return RdpCombinerMode::TextureShadeModulate;
        }
        if (color_a == 1U && color_c == 6U) {
            return RdpCombinerMode::TextureReplace;
        }
        if (color_a == 4U && color_c == 6U) {
            return RdpCombinerMode::ShadeOnly;
        }
        return RdpCombinerMode::Unsupported;
    }

    void capture_combiner(uint32_t word0, uint32_t word1) {
        rdp_combiner.captured = true;
        rdp_combiner.word0 = word0;
        rdp_combiner.word1 = word1;
        rdp_combiner.mode = classify_combiner(word0, word1);
    }
    bool capture_other_mode(
            uint32_t &target,
            bool &captured,
            uint32_t word0,
            uint32_t word1,
            std::string &error) {
        uint32_t shift = (word0 >> 8U) & 0xFFU;
        uint32_t length = word0 & 0xFFU;
        if (microcode_family == N64MicrocodeFamily::F3DEX2) {
            length += 1U;
            if (shift + length > 32U) {
                error.clear();
                return true;
            }
            shift = 32U - shift - length;
        }
        if (shift >= 32U || length > 32U - shift) {
            // Real task streams can use opcode B9/BA for microcode-local state
            // outside the libultra OtherMode layout. Preserve the last known
            // valid state and continue decoding the rest of the display list.
            error.clear();
            return true;
        }
        const uint32_t field_mask = length == 32U ? 0xffffffffU :
                ((UINT32_C(1) << length) - 1U) << shift;
        target = (target & ~field_mask) | (word1 & field_mask);
        captured = true;
        return true;
    }

    RdpRasterState current_raster_state() const {
        RdpRasterState state;
        state.other_mode_low_captured = other_mode_low_captured;
        state.other_mode_low = other_mode_low;
        state.other_mode_high_captured = other_mode_high_captured;
        state.other_mode_high = other_mode_high;
        state.cycle_type = static_cast<uint8_t>(
                (other_mode_high & OTHER_MODE_HIGH_CYCLE_TYPE) >> 20U);
        state.z_buffer_enabled =
                (geometry_mode & GEOMETRY_MODE_ZBUFFER) != 0U;
        state.depth_compare = state.z_buffer_enabled &&
                (other_mode_low & OTHER_MODE_Z_COMPARE) != 0U;
        state.depth_write = state.z_buffer_enabled &&
                (other_mode_low & OTHER_MODE_Z_UPDATE) != 0U;
        state.alpha_compare =
                (other_mode_low & OTHER_MODE_ALPHA_COMPARE) != 0U;
        state.force_blend =
                (other_mode_low & OTHER_MODE_FORCE_BLEND) != 0U;
        state.coverage_destination = static_cast<uint8_t>(
                (other_mode_low & OTHER_MODE_COVERAGE_DESTINATION) >> 8U);
        state.coverage_times_alpha =
                (other_mode_low & OTHER_MODE_COVERAGE_TIMES_ALPHA) != 0U;
        state.alpha_coverage_select =
                (other_mode_low & OTHER_MODE_ALPHA_COVERAGE_SELECT) != 0U;
        return state;
    }
    bool surface_for_state(ModelSurface *&surface, std::string &error) {
        uint32_t texture_index = 0;
        if (!ensure_texture(texture_index, error)) {
            return false;
        }
        uint32_t secondary_texture_index = FAST3D_CAPTURE_NO_INDEX;
        if (rdp_combiner.mode == RdpCombinerMode::TrilinearTextureShade) {
            if (active_tile + 1U >= tile_states.size() ||
                    !ensure_texture_for_tile(active_tile + 1U,
                            secondary_texture_index, error)) {
                return false;
            }
        }
        for (ModelSurface &candidate : output->surfaces) {
            if (candidate.texture_index == texture_index &&
                    candidate.secondary_texture_index == secondary_texture_index &&
                    candidate.render_layer == current_root.render_layer &&
                    candidate.alpha_blend == current_root.alpha_blend &&
                    candidate.alpha_cutout == current_root.alpha_cutout &&
                    candidate.clamp_s == texture_state.clamp_s &&
                    candidate.clamp_t == texture_state.clamp_t &&
                    candidate.cull_front ==
                            ((geometry_mode & cull_front_mask()) != 0U) &&
                    candidate.cull_back ==
                            ((geometry_mode & cull_back_mask()) != 0U) &&
                    candidate.rdp_combiner.captured == rdp_combiner.captured &&
                    candidate.rdp_combiner.word0 == rdp_combiner.word0 &&
                    candidate.rdp_combiner.word1 == rdp_combiner.word1 &&
                    candidate.rdp_combiner.primitive_color_captured ==
                            rdp_combiner.primitive_color_captured &&
                    candidate.rdp_combiner.primitive_color ==
                            rdp_combiner.primitive_color &&
                    candidate.rdp_combiner.environment_color_captured ==
                            rdp_combiner.environment_color_captured &&
                    candidate.rdp_combiner.environment_color ==
                            rdp_combiner.environment_color &&
                    candidate.rdp_raster.other_mode_low_captured ==
                            other_mode_low_captured &&
                    candidate.rdp_raster.other_mode_low == other_mode_low &&
                    candidate.rdp_raster.other_mode_high_captured ==
                            other_mode_high_captured &&
                    candidate.rdp_raster.other_mode_high == other_mode_high &&
                    candidate.rdp_raster.z_buffer_enabled ==
                            ((geometry_mode & GEOMETRY_MODE_ZBUFFER) != 0U)) {
                surface = &candidate;
                return true;
            }
        }
        ModelSurface candidate;
        candidate.texture_index = texture_index;
        candidate.secondary_texture_index = secondary_texture_index;
        candidate.render_layer = current_root.render_layer;
        candidate.alpha_blend = current_root.alpha_blend;
        candidate.alpha_cutout = current_root.alpha_cutout;
        candidate.clamp_s = texture_state.clamp_s;
        candidate.clamp_t = texture_state.clamp_t;
        candidate.cull_front =
                (geometry_mode & cull_front_mask()) != 0U;
        candidate.cull_back =
                (geometry_mode & cull_back_mask()) != 0U;
        candidate.rdp_combiner = rdp_combiner;
        candidate.rdp_raster = current_raster_state();
        output->surfaces.push_back(std::move(candidate));
        surface = &output->surfaces.back();
        return true;
    }

    bool append_triangle(
            uint32_t first,
            uint32_t second,
            uint32_t third,
            uint32_t draw_index,
            std::string &error) {
        const std::array<uint32_t, 3> indices = {first, second, third};
        for (uint32_t index : indices) {
            if (index >= vertex_cache.size() || !vertex_loaded[index]) {
                error = "GBI referenced an unloaded vertex.";
                return false;
            }
        }

        ModelSurface *surface = nullptr;
        if (!surface_for_state(surface, error)) {
            return false;
        }
        const ModelTexture &texture = output->textures[surface->texture_index];
        for (uint32_t index : indices) {
            const DecodedVertex &source = vertex_cache[index];
            ModelVertex vertex;
            vertex.position_x = static_cast<float>(source.position_x);
            vertex.position_y = static_cast<float>(source.position_y);
            vertex.position_z = static_cast<float>(source.position_z);
            vertex.uv_x = (static_cast<float>(source.texture_s) / 32.0F) *
                    texture_scale_s / static_cast<float>(texture.width);
            vertex.uv_y = (static_cast<float>(source.texture_t) / 32.0F) *
                    texture_scale_t / static_cast<float>(texture.height);
            vertex.color_a = static_cast<float>(source.alpha) / 255.0F;
            vertex.fast3d_modelview_matrix_index =
                    source.modelview_matrix_index;
            vertex.fast3d_projection_matrix_index =
                    source.projection_matrix_index;
            vertex.fast3d_viewport_index = source.viewport_index;
            vertex.fast3d_has_screen_xy_override =
                    source.has_screen_xy_override;
            vertex.fast3d_screen_x_s13_2 = source.screen_x_s13_2;
            vertex.fast3d_screen_y_s13_2 = source.screen_y_s13_2;
            vertex.fast3d_has_screen_z_override =
                    source.has_screen_z_override;
            vertex.fast3d_screen_z = source.screen_z;
            if (source.uses_lighting) {
                vertex.normal_x = static_cast<float>(static_cast<int8_t>(source.attribute_x)) / 127.0F;
                vertex.normal_y = static_cast<float>(static_cast<int8_t>(source.attribute_y)) / 127.0F;
                vertex.normal_z = static_cast<float>(static_cast<int8_t>(source.attribute_z)) / 127.0F;
                vertex.color_r = light_red;
                vertex.color_g = light_green;
                vertex.color_b = light_blue;
            } else {
                vertex.normal_y = 1.0F;
                vertex.color_r = static_cast<float>(source.attribute_x) / 255.0F;
                vertex.color_g = static_cast<float>(source.attribute_y) / 255.0F;
                vertex.color_b = static_cast<float>(source.attribute_z) / 255.0F;
            }
            surface->vertices.push_back(vertex);
            surface->indices.push_back(
                    static_cast<uint32_t>(surface->vertices.size() - 1));
        }
        surface->triangle_draw_indices.push_back(draw_index);
        return true;
    }

    bool append_triangle_word(
            uint32_t word,
            uint32_t draw_index,
            std::string &error) {
        return append_triangle(
                ((word >> 16U) & 0xFFU) / triangle_index_divisor,
                ((word >> 8U) & 0xFFU) / triangle_index_divisor,
                (word & 0xFFU) / triangle_index_divisor,
                draw_index,
                error);
    }

    void update_tile_state(uint32_t word0, uint32_t word1) {
        const uint32_t tile_index = (word1 >> 24U) & 0x07U;
        TileState &tile = tile_states[tile_index];
        tile.configured = true;
        tile.format = (word0 >> 21U) & 0x07U;
        tile.size = (word0 >> 19U) & 0x03U;
        tile.line = (word0 >> 9U) & 0x01FFU;
        tile.tmem_word_offset = word0 & 0x01FFU;
        tile.palette = (word1 >> 20U) & 0x0FU;
        tile.clamp_s = (((word1 >> 8U) & 0x03U) & 2U) != 0U;
        tile.clamp_t = (((word1 >> 18U) & 0x03U) & 2U) != 0U;
        if (tile_index == active_tile) {
            texture_state.clamp_s = tile.clamp_s;
            texture_state.clamp_t = tile.clamp_t;
        }
    }

    void update_tile_size(uint32_t word0, uint32_t word1) {
        const uint32_t tile_index = (word1 >> 24U) & 0x07U;
        TileState &tile = tile_states[tile_index];
        const uint32_t upper_s = (word0 >> 12U) & 0x0FFFU;
        const uint32_t upper_t = word0 & 0x0FFFU;
        const uint32_t lower_s = (word1 >> 12U) & 0x0FFFU;
        const uint32_t lower_t = word1 & 0x0FFFU;
        if (lower_s >= upper_s) tile.width = (lower_s - upper_s) / 4U + 1U;
        if (lower_t >= upper_t) tile.height = (lower_t - upper_t) / 4U + 1U;
        if (tile_index == active_tile) {
            texture_state.width = tile.width;
            texture_state.height = tile.height;
            texture_state.clamp_s = tile.clamp_s;
            texture_state.clamp_t = tile.clamp_t;
        }
    }

    bool load_block(uint32_t word0, uint32_t word1, std::string &error) {
        const uint32_t tile_index = (word1 >> 24U) & 0x07U;
        const uint32_t upper_s = ((word0 >> 12U) & 0x0FFFU) / 4U;
        const uint32_t texel_count = ((word1 >> 12U) & 0x0FFFU) + 1U;
        return copy_texture_image_to_tmem(tile_index, upper_s, texel_count, false, error);
    }

    bool load_tile(uint32_t word0, uint32_t word1, std::string &error) {
        const uint32_t tile_index = (word1 >> 24U) & 0x07U;
        const uint32_t upper_s = ((word0 >> 12U) & 0x0FFFU) / 4U;
        const uint32_t upper_t = (word0 & 0x0FFFU) / 4U;
        const uint32_t lower_s = ((word1 >> 12U) & 0x0FFFU) / 4U;
        const uint32_t lower_t = (word1 & 0x0FFFU) / 4U;
        if (lower_s < upper_s || lower_t < upper_t || texture_state.width == 0U) {
            error = "GBI LoadTile has invalid bounds.";
            return false;
        }
        const uint32_t width = lower_s - upper_s + 1U;
        const uint32_t height = lower_t - upper_t + 1U;
        TileState &tile = tile_states[tile_index];
        const uint32_t bits_per_texel = texel_bit_count(texture_state.size);
        const uint32_t packed_row_bytes = (width * bits_per_texel + 7U) / 8U;
        const uint32_t row_bytes = tile.line != 0U ? tile.line * 8U : packed_row_bytes;
        if (bits_per_texel == 0U || static_cast<uint64_t>(tile.tmem_word_offset) * 8U +
                static_cast<uint64_t>(row_bytes) * height > TMEM_BYTE_COUNT) {
            error = "GBI LoadTile exceeds bounded TMEM.";
            return false;
        }
        for (uint32_t row = 0; row < height; ++row) {
            const uint32_t source_texel =
                    (upper_t + row) * texture_state.width + upper_s;
            const uint32_t destination = tile.tmem_word_offset * 8U + row * row_bytes;
            const uint32_t previous_offset = tile.tmem_word_offset;
            tile.tmem_word_offset = destination / 8U;
            if (!copy_texture_image_to_tmem(tile_index, source_texel, width, false, error)) {
                tile.tmem_word_offset = previous_offset;
                return false;
            }
            tile.tmem_word_offset = previous_offset;
        }
        return true;
    }

    bool load_tlut(uint32_t word1, std::string &error) {
        const uint32_t tile_index = (word1 >> 24U) & 0x07U;
        const uint32_t entry_count = ((word1 >> 12U) & 0x0FFFU) + 1U;
        if (texture_state.size != IMAGE_SIZE_16_BIT) {
            error = "GBI LoadTLUT requires a 16-bit palette source.";
            return false;
        }
        return copy_texture_image_to_tmem(tile_index, 0U, entry_count, true, error);
    }
    bool update_light(
            uint32_t word0,
            uint32_t address,
            std::string &error) {
        const uint8_t destination =
                static_cast<uint8_t>((word0 >> 16U) & 0xFFU);
        if (destination != 0x86U) {
            return true;
        }
        const std::vector<uint8_t> *segment = nullptr;
        size_t offset = 0;
        if (!resolve_current_segment_address(
                    address,
                    3,
                    segment,
                    offset,
                    error)) {
            return false;
        }
        light_red = static_cast<float>((*segment)[offset]) / 255.0F;
        light_green = static_cast<float>((*segment)[offset + 1]) / 255.0F;
        light_blue = static_cast<float>((*segment)[offset + 2]) / 255.0F;
        return true;
    }

    bool decode_display_list(
            uint32_t segmented_address,
            size_t depth,
            std::string &error) {
        if (depth > MAX_DISPLAY_LIST_DEPTH) {
            error = "GBI exceeded XR64's display-list recursion limit.";
            return false;
        }
        const std::vector<uint8_t> *segment = nullptr;
        size_t cursor = 0;
        if (!resolve_current_segment_address(
                    segmented_address,
                    8,
                    segment,
                    cursor,
                    error)) {
            return false;
        }

        while (cursor <= segment->size() - 8) {
            if (++command_count > MAX_COMMANDS) {
                error = "GBI exceeded XR64's command safety limit.";
                return false;
            }
            const uint32_t word0 = read_big_endian_u32(*segment, cursor);
            const uint32_t word1 = read_big_endian_u32(*segment, cursor + 4);
            const uint32_t source_command_address =
                    command_address(segmented_address, cursor);
            cursor += 8;
            const uint8_t opcode = static_cast<uint8_t>(word0 >> 24U);
            ++output->fast3d.command_count;

            if (microcode_family == N64MicrocodeFamily::F3DEX2) {
                bool handled = true;
                switch (opcode) {
                    case F3DEX2_COMMAND_NO_OP:
                    case F3DEX2_COMMAND_SP_NO_OP:
                        record_no_op_command();
                        break;
                    case F3DEX2_COMMAND_VERTEX:
                        if (!load_vertices(word0, word1, error)) return false;
                        break;
                    case F3DEX2_COMMAND_MODIFY_VERTEX:
                        if (!modify_vertex(word0, word1, error)) return false;
                        break;
                    case F3DEX2_COMMAND_TRIANGLE_1:
                        if (!append_triangle_word(
                                    word0,
                                    capture_draw_state(source_command_address),
                                    error)) {
                            return false;
                        }
                        break;
                    case F3DEX2_COMMAND_TRIANGLE_2:
                    case F3DEX2_COMMAND_QUAD:
                        if (!append_triangle_word(
                                    word0,
                                    capture_draw_state(source_command_address),
                                    error) ||
                                !append_triangle_word(
                                    word1,
                                    capture_draw_state(source_command_address),
                                    error)) {
                            return false;
                        }
                        break;
                    case F3DEX2_COMMAND_TEXTURE:
                        texture_enabled = ((word0 >> 1U) & 0x7FU) != 0U;
                        active_tile = (word0 >> 8U) & 0x07U;
                        if (texture_enabled) {
                            const uint32_t raw_scale_s = word1 >> 16U;
                            const uint32_t raw_scale_t = word1 & 0xFFFFU;
                            texture_scale_s = raw_scale_s == 0U ? 1.0F :
                                    static_cast<float>(raw_scale_s) / 65536.0F;
                            texture_scale_t = raw_scale_t == 0U ? 1.0F :
                                    static_cast<float>(raw_scale_t) / 65536.0F;
                        }
                        break;
                    case F3DEX2_COMMAND_POP_MATRIX: {
                        if (word1 == 0U || word1 % 64U != 0U) {
                            error = "F3DEX2 G_POPMTX has an invalid byte count.";
                            return false;
                        }
                        const uint32_t count = word1 / 64U;
                        for (uint32_t index = 0U; index < count; ++index) {
                            pop_modelview_matrix();
                        }
                        break;
                    }
                    case F3DEX2_COMMAND_GEOMETRY_MODE:
                        geometry_mode &= word0 & 0x00FFFFFFU;
                        geometry_mode |= word1;
                        break;
                    case F3DEX2_COMMAND_MATRIX:
                        if (!capture_matrix(word0, word1,
                                    source_command_address, error)) {
                            return false;
                        }
                        break;
                    case F3DEX2_COMMAND_MOVE_WORD:
                        if (is_segment_move_word(word0)) {
                            if (!apply_move_word_segment(word0, word1, error)) {
                                return false;
                            }
                        } else {
                            record_passthrough_command(opcode);
                        }
                        break;
                    case F3DEX2_COMMAND_MOVE_MEMORY:
                        if ((word0 & 0xFFU) == F3DEX2_MOVE_MEMORY_VIEWPORT) {
                            if (!capture_viewport(word0, word1,
                                        source_command_address, error)) {
                                return false;
                            }
                        } else {
                            record_passthrough_command(opcode);
                        }
                        break;
                    case F3DEX2_COMMAND_DISPLAY_LIST:
                        if (!decode_display_list(word1, depth + 1U, error)) {
                            return false;
                        }
                        if (((word0 >> 16U) & 1U) != 0U) return true;
                        break;
                    case F3DEX2_COMMAND_END_DISPLAY_LIST:
                        return true;
                    case F3DEX2_COMMAND_RDP_HALF_1:
                    case F3DEX2_COMMAND_RDP_HALF_2:
                        record_passthrough_command(opcode);
                        break;
                    case F3DEX2_COMMAND_SET_OTHER_MODE_L:
                        if (!valid_other_mode_field(word0)) {
                            record_passthrough_command(opcode);
                        } else if (!capture_other_mode(other_mode_low,
                                    other_mode_low_captured,
                                    word0, word1, error)) {
                            return false;
                        }
                        break;
                    case F3DEX2_COMMAND_SET_OTHER_MODE_H:
                        if (!valid_other_mode_field(word0)) {
                            record_passthrough_command(opcode);
                        } else if (!capture_other_mode(other_mode_high,
                                    other_mode_high_captured,
                                    word0, word1, error)) {
                            return false;
                        }
                        break;
                    default:
                        handled = false;
                        break;
                }
                if (handled) continue;
                if (opcode < COMMAND_TEXTURE_RECTANGLE) {
                    return unsupported_opcode(
                            opcode, source_command_address, error);
                }
            }

            switch (opcode) {
                case COMMAND_MATRIX:
                    if (!capture_matrix(
                                word0, word1, source_command_address, error)) {
                        return false;
                    }
                    break;
                case COMMAND_MOVE_MEMORY: {
                    const uint8_t destination =
                            static_cast<uint8_t>((word0 >> 16U) & 0xFFU);
                    if (destination == MOVE_MEMORY_VIEWPORT) {
                        if (!capture_viewport(word0, word1,
                                    source_command_address, error)) {
                            return false;
                        }
                    } else {
                        // Look-at and light slots require the complete F3DEX
                        // light table plus G_MW_NUMLIGHT state. Preserve their
                        // existence honestly instead of guessing a color.
                        record_passthrough_command(opcode);
                    }
                    break;
                }
                case COMMAND_VERTEX:
                    if (!load_vertices(word0, word1, error)) {
                        return false;
                    }
                    break;
                case COMMAND_TRIANGLE_1:
                    if (!append_triangle_word(
                                word1,
                                capture_draw_state(source_command_address),
                                error)) {
                        return false;
                    }
                    break;
                case COMMAND_TRIANGLE_2:
                    if (!append_triangle_word(
                                word0,
                                capture_draw_state(source_command_address),
                                error) ||
                            !append_triangle_word(
                                word1,
                                capture_draw_state(source_command_address),
                                error)) {
                        return false;
                    }
                    break;
                case COMMAND_POP_MATRIX:
                    pop_modelview_matrix();
                    break;
                case COMMAND_MOVE_WORD:
                    if (is_segment_move_word(word0)) {
                        if (!apply_move_word_segment(word0, word1, error)) {
                            return false;
                        }
                    } else {
                        record_passthrough_command(opcode);
                    }
                    break;
                case COMMAND_DISPLAY_LIST: {
                    if (!decode_display_list(word1, depth + 1, error)) {
                        return false;
                    }
                    if (((word0 >> 16U) & 0xFFU) != 0) {
                        return true;
                    }
                    break;
                }
                case COMMAND_SET_OTHER_MODE_L:
                    if (!valid_other_mode_field(word0)) {
                        record_passthrough_command(opcode);
                    } else if (!capture_other_mode(other_mode_low,
                                other_mode_low_captured, word0, word1, error)) {
                        return false;
                    }
                    break;
                case COMMAND_SET_OTHER_MODE_H:
                    if (!valid_other_mode_field(word0)) {
                        record_passthrough_command(opcode);
                    } else if (!capture_other_mode(other_mode_high,
                                other_mode_high_captured, word0, word1, error)) {
                        return false;
                    }
                    break;
                case COMMAND_SET_COMBINE:
                    capture_combiner(word0, word1);
                    break;
                case COMMAND_SET_PRIMITIVE_COLOR:
                    rdp_combiner.primitive_color_captured = true;
                    rdp_combiner.primitive_color = word1;
                    break;
                case COMMAND_SET_ENVIRONMENT_COLOR:
                    rdp_combiner.environment_color_captured = true;
                    rdp_combiner.environment_color = word1;
                    break;
                case COMMAND_SET_TEXTURE_IMAGE:
                    texture_state.address = word1;
                    texture_state.format = (word0 >> 21U) & 0x07U;
                    texture_state.size = (word0 >> 19U) & 0x03U;
                    texture_state.width = (word0 & 0x0FFFU) + 1U;
                    texture_state.height = 1U;
                    break;
                case COMMAND_LOAD_TLUT:
                    if (!load_tlut(word1, error)) return false;
                    break;
                case COMMAND_LOAD_BLOCK:
                    if (!load_block(word0, word1, error)) return false;
                    break;
                case COMMAND_LOAD_TILE:
                    if (!load_tile(word0, word1, error)) return false;
                    break;
                case COMMAND_SET_TILE:
                    update_tile_state(word0, word1);
                    break;
                case COMMAND_SET_TILE_SIZE:
                    update_tile_size(word0, word1);
                    break;
                case COMMAND_SET_GEOMETRY_MODE:
                    geometry_mode |= word1;
                    break;
                case COMMAND_CLEAR_GEOMETRY_MODE:
                    geometry_mode &= ~word1;
                    break;
                case COMMAND_END_DISPLAY_LIST:
                    return true;
                case COMMAND_TEXTURE:
                    texture_enabled = (word0 & 0xFFU) != 0;
                    active_tile = (word0 >> 8U) & 0x07U;
                    if (texture_enabled) {
                        const uint32_t raw_scale_s = word1 >> 16U;
                        const uint32_t raw_scale_t = word1 & 0xFFFFU;
                        texture_scale_s = raw_scale_s == 0U ? 1.0F :
                                static_cast<float>(raw_scale_s) / 65536.0F;
                        texture_scale_t = raw_scale_t == 0U ? 1.0F :
                                static_cast<float>(raw_scale_t) / 65536.0F;
                    }
                    break;
                case COMMAND_LOAD_SYNC:
                case COMMAND_PIPE_SYNC:
                case COMMAND_TILE_SYNC:
                case COMMAND_FULL_SYNC:
                    record_no_op_command();
                    break;
                case COMMAND_TEXTURE_RECTANGLE:
                case COMMAND_TEXTURE_RECTANGLE_FLIP:
                case COMMAND_SET_KEY_GB:
                case COMMAND_SET_KEY_R:
                case COMMAND_SET_CONVERT:
                case COMMAND_SET_SCISSOR:
                case COMMAND_SET_PRIMITIVE_DEPTH:
                case COMMAND_RDP_SET_OTHER_MODE:
                case COMMAND_FILL_RECTANGLE:
                case COMMAND_SET_FILL_COLOR:
                case COMMAND_SET_FOG_COLOR:
                case COMMAND_SET_BLEND_COLOR:
                case COMMAND_SET_DEPTH_IMAGE:
                case COMMAND_SET_COLOR_IMAGE:
                    // These are known standard commands, but XR64 does not yet
                    // claim a Godot-scene equivalent. The atomic bundle keeps
                    // their exact words and the normalized event records this
                    // explicit passthrough coverage.
                    record_passthrough_command(opcode);
                    break;
                default:
                    return unsupported_opcode(
                            opcode, source_command_address, error);
            }        }
        error = "A GBI display list ended outside its ROM segment.";
        return false;
    }
};

} // namespace

bool decode_n64_gbi_scene(
        N64MicrocodeFamily microcode_family,
        const N64SegmentMap &segments,
        const std::vector<DisplayListRoot> &roots,
        RenderSceneData &scene,
        std::string &error) {
    N64GbiDecoder decoder(microcode_family, segments);
    return decoder.decode(roots, scene, error);
}

bool decode_n64_gbi_actor_parts(
        N64MicrocodeFamily microcode_family,
        const N64SegmentMap &segments,
        const std::vector<ActorDisplayListRoot> &roots,
        std::vector<RenderSceneData> &part_models,
        std::string &error) {
    N64GbiDecoder decoder(microcode_family, segments);
    return decoder.decode_actor_parts(roots, part_models, error);
}

bool decode_fast3d_scene(
        const N64SegmentMap &segments,
        const std::vector<DisplayListRoot> &roots,
        RenderSceneData &scene,
        std::string &error) {
    return decode_n64_gbi_scene(
            N64MicrocodeFamily::Fast3D, segments, roots, scene, error);
}

bool decode_fast3d_actor_parts(
        const N64SegmentMap &segments,
        const std::vector<ActorDisplayListRoot> &roots,
        std::vector<RenderSceneData> &part_models,
        std::string &error) {
    return decode_n64_gbi_actor_parts(
            N64MicrocodeFamily::Fast3D,
            segments, roots, part_models, error);
}

} // namespace xr64
