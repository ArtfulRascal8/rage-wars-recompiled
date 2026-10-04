#include "render_diagnostics.hpp"
#include "n64_raw_fast3d_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>

namespace xr64 {
namespace {

constexpr std::uint8_t kF3dNoop = 0xC0;
constexpr std::uint8_t kF3dMtx = 0x01;
constexpr std::uint8_t kF3dMoveMem = 0x03;
constexpr std::uint8_t kF3dVtx = 0x04;
constexpr std::uint8_t kF3dDl = 0x06;
constexpr std::uint8_t kF3dTri1 = 0xBF;
constexpr std::uint8_t kF3dPopMtx = 0xBD;
constexpr std::uint8_t kF3dMoveWord = 0xBC;
constexpr std::uint8_t kF3dTexture = 0xBB;
constexpr std::uint8_t kF3dModeH = 0xBA;
constexpr std::uint8_t kF3dModeL = 0xB9;
constexpr std::uint8_t kF3dEnd = 0xB8;
constexpr std::uint8_t kF3dSetGeom = 0xB7;
constexpr std::uint8_t kF3dClearGeom = 0xB6;
constexpr std::uint8_t kF3dRdpHalf1 = 0xB4;
constexpr std::uint8_t kF3dRdpHalf2 = 0xB3;
constexpr std::uint8_t kF3dTri4 = 0xB1;

constexpr std::uint8_t kF3dex2Vtx = 0x01;
constexpr std::uint8_t kF3dex2ModifyVtx = 0x02;
constexpr std::uint8_t kF3dex2Tri1 = 0x05;
constexpr std::uint8_t kF3dex2Tri2 = 0x06;
constexpr std::uint8_t kF3dex2Quad = 0x07;
constexpr std::uint8_t kF3dex2Texture = 0xD7;
constexpr std::uint8_t kF3dex2PopMtx = 0xD8;
constexpr std::uint8_t kF3dex2Geom = 0xD9;
constexpr std::uint8_t kF3dex2Mtx = 0xDA;
constexpr std::uint8_t kF3dex2MoveWord = 0xDB;
constexpr std::uint8_t kF3dex2MoveMem = 0xDC;
constexpr std::uint8_t kF3dex2Dl = 0xDE;
constexpr std::uint8_t kF3dex2End = 0xDF;
constexpr std::uint8_t kF3dex2Noop = 0xE0;
constexpr std::uint8_t kF3dex2Half1 = 0xE1;
constexpr std::uint8_t kF3dex2ModeL = 0xE2;
constexpr std::uint8_t kF3dex2ModeH = 0xE3;
constexpr std::uint8_t kF3dex2Half2 = 0xF1;

constexpr std::uint8_t kF3dex2ModifyVtxRgba = 0x10;
constexpr std::uint8_t kF3dex2ModifyVtxSt = 0x14;
constexpr std::uint8_t kF3dex2ModifyVtxXyScreen = 0x18;
constexpr std::uint8_t kF3dex2ModifyVtxZScreen = 0x1C;

constexpr std::uint8_t kSetCImg = 0xFF;
constexpr std::uint8_t kSetZImg = 0xFE;
constexpr std::uint8_t kSetTImg = 0xFD;
constexpr std::uint8_t kSetCombine = 0xFC;
constexpr std::uint8_t kSetEnv = 0xFB;
constexpr std::uint8_t kSetPrim = 0xFA;
constexpr std::uint8_t kSetBlend = 0xF9;
constexpr std::uint8_t kSetFog = 0xF8;
constexpr std::uint8_t kSetFill = 0xF7;
constexpr std::uint8_t kFillRect = 0xF6;
constexpr std::uint8_t kSetTile = 0xF5;
constexpr std::uint8_t kLoadTile = 0xF4;
constexpr std::uint8_t kLoadBlock = 0xF3;
constexpr std::uint8_t kSetTileSize = 0xF2;
constexpr std::uint8_t kLoadTlut = 0xF0;
constexpr std::uint8_t kRdpMode = 0xEF;
constexpr std::uint8_t kPrimDepth = 0xEE;
constexpr std::uint8_t kScissor = 0xED;
constexpr std::uint8_t kSetConvert = 0xEC;
constexpr std::uint8_t kSetKeyR = 0xEB;
constexpr std::uint8_t kSetKeyGb = 0xEA;
constexpr std::uint8_t kFullSync = 0xE9;
constexpr std::uint8_t kTileSync = 0xE8;
constexpr std::uint8_t kPipeSync = 0xE7;
constexpr std::uint8_t kLoadSync = 0xE6;
constexpr std::uint8_t kTexRectFlip = 0xE5;
constexpr std::uint8_t kTexRect = 0xE4;

constexpr std::uint32_t kKseg0 = 0x80000000U;
constexpr std::uint32_t kKseg1 = 0xA0000000U;
constexpr std::uint32_t kPhysicalMask = 0x1FFFFFFFU;
constexpr std::uint32_t kGeometryModeLighting = 0x00020000U;
constexpr std::size_t kMaxCommands = 100000U;
constexpr std::size_t kMaxDepth = 32U;
constexpr std::size_t kMaxVertices = 64U;
constexpr std::size_t kMaxDirectionalLights = 7U;
constexpr std::size_t kTmemBytes = 4096U;

using Matrix = std::array<float, 16>;

struct Tile {
    std::uint32_t descriptor_word1 = 0;
    std::uint32_t format = 0;
    std::uint32_t size = 2;
    std::uint32_t line = 0;
    std::uint32_t tmem = 0;
    std::uint32_t palette = 0;
    std::uint32_t cms = 0;
    std::uint32_t cmt = 0;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t uls = 0;
    std::uint32_t ult = 0;
    std::uint32_t lrs = 0;
    std::uint32_t lrt = 0;
};

struct ImageState {
    std::uint32_t address = 0;
    std::uint32_t format = 0;
    std::uint32_t size = 2;
    std::uint32_t width = 1;
};

struct TextureDecodeCacheKey {
    std::uint8_t tile_index = 0;
    bool rectangle = false;
    Tile tile{};
    Tile mask_tile{};
    std::uint64_t other_mode = 0;
    std::uint32_t combine_word0 = 0;
    std::uint32_t combine_word1 = 0;
    std::uint8_t environment_alpha = 255;
};

struct TextureDecodeContent {
    // Ordered samples exactly match the TMEM reads and TLUT lookups in decode.
    std::vector<std::uint8_t> tmem_bytes;
    std::vector<std::uint16_t> tlut_values;
};

struct SharedTextureDecodeEntry {
    TextureDecodeCacheKey key;
    TextureDecodeContent content;
    std::shared_ptr<const N64RawFast3DTexture> texture;
};

struct SharedTextureDecodeCacheSlot {
    std::mutex mutex;
    std::shared_ptr<const SharedTextureDecodeEntry> entry;
};

SharedTextureDecodeCacheSlot &shared_texture_decode_cache() {
    static SharedTextureDecodeCacheSlot cache;
    return cache;
}

constexpr std::size_t kSharedTextureDecodeCacheMaxBytes = 8U * 1024U * 1024U;

struct StandardLight {
    N64RawFast3DColor color{};
    float direction_x = 0.0F;
    float direction_y = 0.0F;
    float direction_z = 0.0F;
    bool loaded = false;
};

std::uint16_t color565_to_rgba16(std::uint16_t value) {
    const std::uint16_t r = static_cast<std::uint16_t>((value >> 11U) & 31U);
    const std::uint16_t g = static_cast<std::uint16_t>((value >> 6U) & 31U);
    const std::uint16_t b = static_cast<std::uint16_t>((value >> 1U) & 31U);
    const std::uint16_t a = static_cast<std::uint16_t>(value & 1U);
    return static_cast<std::uint16_t>((r << 11U) | (g << 6U) | (b << 1U) | a);
}

std::uint8_t expand5(std::uint32_t value) {
    return static_cast<std::uint8_t>((value << 3U) | (value >> 2U));
}

std::size_t bytes_per_texel(std::uint32_t size) {
    switch (size) {
        case 0: return 1; // 4b, rounded by callers where needed
        case 1: return 1;
        case 2: return 2;
        case 3: return 4;
        default: return 0;
    }
}

class Executor {
    N64RawFast3DListReplacement *replacement_=nullptr;
    bool suppress_replaced_=false;
public:
    Executor(const N64GraphicsTaskDescriptor &task, const std::uint8_t *rdram,
            std::size_t rdram_size, N64RawFast3DBackend &backend,
            std::string &error, N64RawFast3DTaskStats *stats, N64RawFast3DListReplacement *replacement) : replacement_(replacement), task_(task), rdram_(rdram),
            rdram_size_(rdram_size), backend_(backend), error_(error), stats_(stats) {
        for (Matrix &matrix : modelview_) matrix = identity();
        projection_ = identity();
        modelview_depth_ = 0;
        update_mp();
        segments_[0] = 0;
        segment_loaded_[0] = true;
        viewport_width_ = 320;
        viewport_height_ = 240;
    }

    bool run() {
        if (rdram_ == nullptr || rdram_size_ == 0) {
            error_ = "raw_fast3d_invalid_rdram";
            return false;
        }
        if (task_.microcode_family != N64MicrocodeFamily::F3DEX2 &&
                task_.microcode_family != N64MicrocodeFamily::F3DEX &&
                task_.microcode_family != N64MicrocodeFamily::Fast3D) {
            error_ = "raw_fast3d_unsupported_microcode_family";
            return false;
        }
        if (!backend_.begin_frame(640, 480, error_)) return false;
        if (!execute_list(task_.task_data_address, 0)) return false;
        if (!backend_.end_frame(error_)) return false;
        if (diagnostic_task_selected() ||
                ::xr64::render_diagnostics::capture_selected(task_.sequence)) {
            std::fprintf(stderr,
                    "RW105_TEXTURE_DECODE_CACHE task=%llu hits=%llu misses=%llu shared_hits=%llu\n",
                    static_cast<unsigned long long>(task_.sequence),
                    static_cast<unsigned long long>(texture_cache_hits_ + shared_texture_cache_hits_),
                    static_cast<unsigned long long>(texture_cache_misses_),
                    static_cast<unsigned long long>(shared_texture_cache_hits_));
            XR64_RENDER_DIAGNOSTIC_FLUSH();
        }
        return true;
    }

private:
    struct TextureDecodeCache {
        bool valid = false;
        TextureDecodeCacheKey key{};
        std::uint64_t tmem_revision = 0;
        std::uint64_t palette_revision = 0;
        std::shared_ptr<const N64RawFast3DTexture> texture;
    };

    const N64GraphicsTaskDescriptor &task_;
    const std::uint8_t *rdram_ = nullptr;
    std::size_t rdram_size_ = 0;
    N64RawFast3DBackend &backend_;
    std::string &error_;
    N64RawFast3DTaskStats *stats_ = nullptr;
    std::array<std::uint32_t, 32> segments_{};
    std::array<bool, 32> segment_loaded_{};
    std::array<N64RawFast3DVertex, kMaxVertices> vertices_{};
    std::array<bool, kMaxVertices> vertex_loaded_{};
    // Slots 0..num_lights-1 are directional; slot num_lights is ambient.
    std::array<StandardLight, kMaxDirectionalLights + 1U> lights_{};
    std::size_t num_lights_ = 0;
    std::array<Matrix, 16> modelview_{};
    bool camera_attachment_=false;
    Matrix projection_{};
    Matrix mp_{};
    std::size_t modelview_depth_ = 0;
    std::uint32_t geometry_mode_ = 0;
    bool texture_enabled_ = false;
    std::uint8_t active_tile_ = 0;
    std::uint32_t texture_scale_s_ = 0x10000;
    std::uint32_t texture_scale_t_ = 0x10000;
    std::array<Tile, 8> tiles_{};
    ImageState image_{};
    // Guest-byte-order physical TMEM: odd texture rows exchange the two
    // 32-bit halves of each 64-bit word. This is separate from RDRAM endian XOR.
    std::array<std::uint8_t, kTmemBytes> tmem_{};
    std::array<std::uint16_t, 256> palette_{};
    std::uint64_t tmem_revision_ = 0;
    std::uint64_t palette_revision_ = 0;
    TextureDecodeCache texture_decode_cache_{};
    std::uint64_t texture_cache_hits_ = 0;
    std::uint64_t shared_texture_cache_hits_ = 0;
    std::uint64_t texture_cache_misses_ = 0;
    std::uint64_t other_mode_ = 0;
    std::uint32_t combine_word0_ = 0;
    std::uint32_t combine_word1_ = 0;
    N64RawFast3DColor primitive_color_{};
    N64RawFast3DColor environment_color_{};
    N64RawFast3DColor fill_color_{};
    int viewport_width_ = 320;
    int viewport_height_ = 240;
    std::int16_t viewport_scale_x_ = 640;
    std::int16_t viewport_scale_y_ = 480;
    std::int16_t viewport_scale_z_ = 511;
    std::int16_t viewport_translate_x_ = 640;
    std::int16_t viewport_translate_y_ = 480;
    std::int16_t viewport_translate_z_ = 511;
    std::size_t command_count_ = 0;
    std::size_t diagnostic_vertex_count_ = 0;
    bool diagnostic_texture_captured_ = false;

    static Matrix identity() {
        Matrix result{};
        result[0] = result[5] = result[10] = result[15] = 1.0F;
        return result;
    }

    static Matrix multiply(const Matrix &a, const Matrix &b) {
        Matrix result{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                for (int k = 0; k < 4; ++k) {
                    result[row * 4 + col] += a[row * 4 + k] *
                            b[k * 4 + col];
                }
            }
        }
        return result;
    }

    void update_mp() { mp_ = multiply(modelview_[modelview_depth_], projection_); }

    bool valid_range(std::size_t address, std::size_t size) const {
        return address <= rdram_size_ && size <= rdram_size_ - address;
    }

    std::uint8_t byte_at(std::size_t physical) const {
        return rdram_[physical ^ 3U];
    }

    bool resolve(std::uint32_t address, std::size_t &physical) {
        const std::uint32_t region = address & 0xE0000000U;
        std::uint64_t result = 0;
        if (region == kKseg0 || region == kKseg1) {
            result = address & kPhysicalMask;
        } else {
            const std::uint32_t segment = address >> 24U;
            if (segment >= segments_.size() || !segment_loaded_[segment]) {
                std::ostringstream stream;
                stream << "raw_fast3d_unset_segment_" << segment;
                error_ = stream.str();
                return false;
            }
            result = static_cast<std::uint64_t>(segments_[segment]) +
                    (address & 0x00FFFFFFU);
        }
        if (result > rdram_size_) {
            error_ = "raw_fast3d_address_outside_rdram";
            return false;
        }
        physical = static_cast<std::size_t>(result);
        return true;
    }

    bool read_u32(std::uint32_t address, std::uint32_t &value) {
        std::size_t physical = 0;
        if (!resolve(address, physical) || !valid_range(physical, 4)) return false;
        value = (static_cast<std::uint32_t>(byte_at(physical)) << 24U) |
                (static_cast<std::uint32_t>(byte_at(physical + 1)) << 16U) |
                (static_cast<std::uint32_t>(byte_at(physical + 2)) << 8U) |
                static_cast<std::uint32_t>(byte_at(physical + 3));
        return true;
    }

    bool read_u16(std::uint32_t address, std::uint16_t &value) {
        std::size_t physical = 0;
        if (!resolve(address, physical) || !valid_range(physical, 2)) return false;
        value = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(byte_at(physical)) << 8U) |
                static_cast<std::uint16_t>(byte_at(physical + 1)));
        return true;
    }

    bool read_i16(std::uint32_t address, std::int16_t &value) {
        std::uint16_t raw = 0;
        if (!read_u16(address, raw)) return false;
        value = static_cast<std::int16_t>(raw);
        return true;
    }

    bool read_matrix(std::uint32_t address, Matrix &matrix) {
        std::array<std::int16_t, 16> integer{};
        std::array<std::uint16_t, 16> fraction{};
        for (std::size_t index = 0; index < 16; ++index) {
            if (!read_i16(address + static_cast<std::uint32_t>(index * 2),
                        integer[index]) ||
                    !read_u16(address + static_cast<std::uint32_t>(32 + index * 2),
                        fraction[index])) return false;
            matrix[index] = static_cast<float>(integer[index]) +
                    static_cast<float>(fraction[index]) / 65536.0F;
        }
        return true;
    }

    bool read_vertex(std::uint32_t address, N64RawFast3DVertex &vertex) {
        std::int16_t x = 0, y = 0, z = 0, s = 0, t = 0;
        if (!read_i16(address + 0, x) || !read_i16(address + 2, y) ||
                !read_i16(address + 4, z) || !read_i16(address + 8, s) ||
                !read_i16(address + 10, t)) return false;
        std::size_t color_physical = 0;
        if (!resolve(address + 12, color_physical) ||
                !valid_range(color_physical, 4)) return false;
        const float px = static_cast<float>(x);
        const float py = static_cast<float>(y);
        const float pz = static_cast<float>(z);
        vertex.x = px * mp_[0] + py * mp_[4] + pz * mp_[8] + mp_[12];
        vertex.y = px * mp_[1] + py * mp_[5] + pz * mp_[9] + mp_[13];
        vertex.z = px * mp_[2] + py * mp_[6] + pz * mp_[10] + mp_[14];
        vertex.w = px * mp_[3] + py * mp_[7] + pz * mp_[11] + mp_[15];
        vertex.s = static_cast<float>(s) / 32.0F;
        vertex.t = static_cast<float>(t) / 32.0F;
        vertex.attributes = {
                byte_at(color_physical),
                byte_at(color_physical + 1),
                byte_at(color_physical + 2),
                byte_at(color_physical + 3)};
        vertex.geometry_mode_at_load = geometry_mode_;
        vertex.color = (geometry_mode_ & kGeometryModeLighting) != 0U ? shade_lit_vertex(vertex.attributes) : vertex.attributes;
        if (diagnostic_task_selected()) {
            std::fprintf(stderr, "RW084_SOURCE_VERTEX task=%llu address=0x%08X object=(%d,%d,%d) st=(%d,%d) geom=0x%08X clip=(%.9g,%.9g,%.9g,%.9g) mv=",
                    static_cast<unsigned long long>(task_.sequence), address, x, y, z, s, t,
                    geometry_mode_, vertex.x, vertex.y, vertex.z, vertex.w);
            for (float value : modelview_[modelview_depth_]) std::fprintf(stderr, "%.9g,", value);
            std::fprintf(stderr, " projection=");
            for (float value : projection_) std::fprintf(stderr, "%.9g,", value);
            std::fprintf(stderr, "\n");
        }
        trace_vertex_load(address, vertex);
        return true;
    }
    static bool normalize(float &x, float &y, float &z) {
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= std::numeric_limits<float>::epsilon()) return false;
        x /= length;
        y /= length;
        z /= length;
        return true;
    }

    N64RawFast3DColor shade_lit_vertex(const N64RawFast3DColor &attributes) const {
        float nx = static_cast<float>(static_cast<std::int8_t>(attributes.r));
        float ny = static_cast<float>(static_cast<std::int8_t>(attributes.g));
        float nz = static_cast<float>(static_cast<std::int8_t>(attributes.b));
        // G_VTX normals are in model space. Apply the active model-view
        // rotation before comparing them to the eye-space light directions.
        const Matrix &modelview = modelview_[modelview_depth_];
        const float eye_x = nx * modelview[0] + ny * modelview[4] + nz * modelview[8];
        const float eye_y = nx * modelview[1] + ny * modelview[5] + nz * modelview[9];
        const float eye_z = nx * modelview[2] + ny * modelview[6] + nz * modelview[10];
        nx = eye_x;
        ny = eye_y;
        nz = eye_z;
        if (!normalize(nx, ny, nz)) return {0, 0, 0, attributes.a};

        float red = 0.0F;
        float green = 0.0F;
        float blue = 0.0F;
        if (num_lights_ < lights_.size() && lights_[num_lights_].loaded) {
            red = static_cast<float>(lights_[num_lights_].color.r);
            green = static_cast<float>(lights_[num_lights_].color.g);
            blue = static_cast<float>(lights_[num_lights_].color.b);
        }
        for (std::size_t index = 0; index < num_lights_; ++index) {
            const StandardLight &light = lights_[index];
            if (!light.loaded) continue;
            const float contribution = std::max(0.0F, nx * light.direction_x +
                    ny * light.direction_y + nz * light.direction_z);
            red += static_cast<float>(light.color.r) * contribution;
            green += static_cast<float>(light.color.g) * contribution;
            blue += static_cast<float>(light.color.b) * contribution;
        }
        const auto clamp_color = [](float value) {
            return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value), 0, 255));
        };
        return {clamp_color(red), clamp_color(green), clamp_color(blue), attributes.a};
    }

    bool load_standard_f3dex2_light(std::uint32_t word0, std::uint32_t address) {
        if ((word0 & 0xFFU) != 0x0AU) return true;
        const std::uint32_t offset = (word0 >> 5U) & 0x7F8U;
        // This Gauntlet draw uses the stock F3DEX2 one-light layout: its
        // directional and ambient records are at offsets 48 and 72. Do not
        // reinterpret later Acclaim-only extended records as this layout.
        if (offset != 48U && offset != 72U) return true;
        const std::size_t slot = (offset - 48U) / 24U;
        std::size_t physical = 0;
        if (!resolve(address, physical) || !valid_range(physical, 11U)) {
            error_ = "raw_fast3d_light_load_failed";
            return false;
        }
        StandardLight &light = lights_[slot];
        light.color = {byte_at(physical), byte_at(physical + 1U),
                byte_at(physical + 2U), 255};
        light.direction_x = static_cast<float>(static_cast<std::int8_t>(byte_at(physical + 8U)));
        light.direction_y = static_cast<float>(static_cast<std::int8_t>(byte_at(physical + 9U)));
        light.direction_z = static_cast<float>(static_cast<std::int8_t>(byte_at(physical + 10U)));
        normalize(light.direction_x, light.direction_y, light.direction_z);
        light.loaded = true;
        return true;
    }


#if XR64_RENDER_DIAGNOSTICS
    bool diagnostic_task_selected() const {
        const char *value = std::getenv("XR64_RW_DIAG_TASK");
        if (value == nullptr || value[0] == '\0') return false;
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 10);
        return end != value && *end == '\0' && parsed == task_.sequence;
    }

    void trace_vertex_load(std::uint32_t address, const N64RawFast3DVertex &vertex) {
        const bool lighting = (vertex.geometry_mode_at_load & kGeometryModeLighting) != 0U;
        if (!diagnostic_task_selected() || !lighting || diagnostic_vertex_count_++ >= 48U) return;
        std::fprintf(stderr,
                "RW079_VERTEX_LOAD task=%llu address=0x%08X geometry=0x%08X "
                "attribute_u8=(%u,%u,%u,%u) attribute_s8=(%d,%d,%d) "
                "interpretation=%s legacy_shade_rgba=(%u,%u,%u,%u)\n",
                static_cast<unsigned long long>(task_.sequence), address,
                vertex.geometry_mode_at_load,
                static_cast<unsigned>(vertex.attributes.r), static_cast<unsigned>(vertex.attributes.g),
                static_cast<unsigned>(vertex.attributes.b), static_cast<unsigned>(vertex.attributes.a),
                static_cast<int>(static_cast<std::int8_t>(vertex.attributes.r)),
                static_cast<int>(static_cast<std::int8_t>(vertex.attributes.g)),
                static_cast<int>(static_cast<std::int8_t>(vertex.attributes.b)),
                lighting ? "normal" : "rgba",
                static_cast<unsigned>(vertex.color.r), static_cast<unsigned>(vertex.color.g),
                static_cast<unsigned>(vertex.color.b), static_cast<unsigned>(vertex.color.a));
        XR64_RENDER_DIAGNOSTIC_FLUSH();
    }

    void trace_light_move_mem(std::uint32_t word0, std::uint32_t address) {
        if (!diagnostic_task_selected() || (word0 & 0xFFU) != 0x0AU) return;
        const std::uint32_t offset = (word0 >> 5U) & 0x7F8U;
        std::size_t physical = 0;
        if (!resolve(address, physical) || !valid_range(physical, 16U)) {
            std::fprintf(stderr, "RW079_MOVEMEM_LIGHT task=%llu offset=%u address=0x%08X bytes=unavailable\n",
                    static_cast<unsigned long long>(task_.sequence), offset, address);
            XR64_RENDER_DIAGNOSTIC_FLUSH();
            return;
        }
        std::fprintf(stderr, "RW079_MOVEMEM_LIGHT task=%llu offset=%u address=0x%08X bytes=",
                static_cast<unsigned long long>(task_.sequence), offset, address);
        for (std::size_t index = 0; index < 16U; ++index) {
            std::fprintf(stderr, "%02X", byte_at(physical + index));
        }
        std::fputc('\n', stderr);
        XR64_RENDER_DIAGNOSTIC_FLUSH();
    }

#else
    bool diagnostic_task_selected() const { return ::xr64::render_diagnostics::capture_selected(task_.sequence); }
    void trace_vertex_load(std::uint32_t, const N64RawFast3DVertex &) {}
    void trace_light_move_mem(std::uint32_t, std::uint32_t) {}
#endif

    bool apply_matrix(std::uint32_t word0, std::uint32_t address) {
        Matrix matrix{};
        if (!read_matrix(address, matrix)) {
            error_ = "raw_fast3d_matrix_outside_rdram";
            return false;
        }
        std::uint8_t parameters = static_cast<std::uint8_t>((word0 >> 16U) & 0xFFU);
        if (task_.microcode_family == N64MicrocodeFamily::F3DEX2) {
            parameters = static_cast<std::uint8_t>((word0 & 0xFFU) ^ 1U);
        }
        const bool projection_matrix = (parameters &
                (task_.microcode_family == N64MicrocodeFamily::F3DEX2 ?
                        0x04U : 0x01U)) != 0;
        const bool load = (parameters & 0x02U) != 0;
        const bool push = (parameters & (task_.microcode_family ==
                N64MicrocodeFamily::F3DEX2 ? 0x01U : 0x04U)) != 0;
        if (projection_matrix) {
            projection_ = load ? matrix : multiply(matrix, projection_);
        } else {
            if (push && modelview_depth_ + 1 < modelview_.size()) {
                modelview_[modelview_depth_ + 1] = modelview_[modelview_depth_];
                ++modelview_depth_;
            }
            modelview_[modelview_depth_] = load ? matrix :
                    multiply(matrix, modelview_[modelview_depth_]);
        }
        update_mp();
        return true;
    }

    N64RawFast3DDrawState draw_state() const {
        N64RawFast3DDrawState state;
        state.source_projection=projection_;
        state.camera_attachment=camera_attachment_;
        // Camera translation can make projection_[15] nonzero; perspective
        // is identified by position-dependent homogeneous W.
        state.perspective_projection = (std::abs(projection_[3]) + std::abs(projection_[7]) + std::abs(projection_[11])) > 0.00001F;
        if (state.perspective_projection) {
            const float yn=std::sqrt(projection_[1]*projection_[1]+projection_[5]*projection_[5]+projection_[9]*projection_[9]);
            const float wn=std::sqrt(projection_[3]*projection_[3]+projection_[7]*projection_[7]+projection_[11]*projection_[11]);
            if (wn>0.00001F) state.perspective_y_scale=yn/wn;
            state.perspective_x_norm=std::sqrt(projection_[0]*projection_[0]+projection_[4]*projection_[4]+projection_[8]*projection_[8]);
            state.perspective_y_norm=yn;
            state.perspective_w_norm=wn;
        }
        state.textured = texture_enabled_;
        state.other_mode = other_mode_;
        state.combine_word0 = combine_word0_;
        state.combine_word1 = combine_word1_;
        state.primitive_color = primitive_color_;
        state.environment_color = environment_color_;
        state.fill_color = fill_color_;
        state.alpha_blend = (other_mode_ & (1ULL << 14U)) != 0;
        state.depth_test = (geometry_mode_ & 0x00000001U) != 0;
        state.depth_write = (other_mode_ & (1ULL << 5U)) != 0;
        state.depth_compare = (other_mode_ & (1ULL << 4U)) != 0;
        state.viewport = {viewport_scale_x_, viewport_scale_y_, viewport_scale_z_,
                viewport_translate_x_, viewport_translate_y_, viewport_translate_z_};
        state.geometry_mode = geometry_mode_;
        const bool f3dex2 = task_.microcode_family == N64MicrocodeFamily::F3DEX2;
        state.cull_front = (geometry_mode_ & (f3dex2 ? 0x200U : 0x1000U)) != 0;
        state.cull_back = (geometry_mode_ & (f3dex2 ? 0x400U : 0x2000U)) != 0;
        return state;
    }

    unsigned diagnostic_upload_ = 0;

    static bool same_tile_state(const Tile &left, const Tile &right) {
        return left.descriptor_word1 == right.descriptor_word1 &&
                left.format == right.format && left.size == right.size &&
                left.line == right.line && left.tmem == right.tmem &&
                left.palette == right.palette && left.cms == right.cms &&
                left.cmt == right.cmt && left.width == right.width &&
                left.height == right.height && left.uls == right.uls &&
                left.ult == right.ult && left.lrs == right.lrs &&
                left.lrt == right.lrt;
    }

    TextureDecodeCacheKey make_texture_cache_key(std::uint8_t tile_index,
            const Tile &tile, bool rectangle) const {
        TextureDecodeCacheKey key;
        key.tile_index = tile_index;
        key.rectangle = rectangle;
        key.tile = tile;
        key.mask_tile = tiles_[(tile_index + 1U) & 7U];
        key.other_mode = other_mode_;
        key.combine_word0 = combine_word0_;
        key.combine_word1 = combine_word1_;
        key.environment_alpha = environment_color_.a;
        return key;
    }

    static bool same_texture_cache_key(const TextureDecodeCacheKey &left,
            const TextureDecodeCacheKey &right) {
        return left.tile_index == right.tile_index &&
                left.rectangle == right.rectangle &&
                same_tile_state(left.tile, right.tile) &&
                same_tile_state(left.mask_tile, right.mask_tile) &&
                left.other_mode == right.other_mode &&
                left.combine_word0 == right.combine_word0 &&
                left.combine_word1 == right.combine_word1 &&
                left.environment_alpha == right.environment_alpha;
    }

    bool should_precombine_hud_alpha(std::uint8_t tile_index,
            const Tile &tile, std::uint32_t width, std::uint32_t height,
            bool rectangle) const {
        const Tile &mask = tiles_[(tile_index + 1U) & 7U];
        return rectangle && ((other_mode_ >> 52U) & 3U) == 1U &&
                combine_word0_ == 0xFC12ABFFU && combine_word1_ == 0xFFFFFE38U &&
                tile.format == 0 && tile.size == 2 &&
                mask.format == 4 && mask.size == 0 &&
                mask.line * 8U == (width + 1U) / 2U &&
                mask.uls == tile.uls && mask.ult == tile.ult &&
                (mask.descriptor_word1 & 0xFFFFFU) ==
                        (tile.descriptor_word1 & 0xFFFFFU) &&
                std::size_t(mask.tmem) * 8U +
                        std::size_t(mask.line) * 8U * height <= tmem_.size();
    }

    bool texture_cache_matches(const TextureDecodeCacheKey &key) const {
        const TextureDecodeCache &cache = texture_decode_cache_;
        return cache.valid && same_texture_cache_key(cache.key, key) &&
                cache.tmem_revision == tmem_revision_ &&
                cache.palette_revision == palette_revision_;
    }

    void remember_texture(const TextureDecodeCacheKey &key,
            const std::shared_ptr<const N64RawFast3DTexture> &texture) {
        TextureDecodeCache &cache = texture_decode_cache_;
        cache.key = key;
        cache.tmem_revision = tmem_revision_;
        cache.palette_revision = palette_revision_;
        cache.texture = texture;
        cache.valid = true;
    }

    std::shared_ptr<const SharedTextureDecodeEntry> shared_cache_snapshot() const {
        SharedTextureDecodeCacheSlot &slot = shared_texture_decode_cache();
        const std::lock_guard<std::mutex> guard(slot.mutex);
        return slot.entry;
    }

    bool shared_texture_inputs_match(const SharedTextureDecodeEntry &entry,
            const TextureDecodeCacheKey &key, const Tile &tile,
            std::uint32_t width, std::uint32_t height,
            std::size_t source_stride, std::size_t base,
            bool precombine_alpha) const {
        if (!same_texture_cache_key(entry.key, key)) return false;
        const TextureDecodeContent &content = entry.content;
        std::size_t tmem_index = 0;
        std::size_t tlut_index = 0;
        const auto compare_tmem = [&](std::size_t address) {
            return address < tmem_.size() &&
                    tmem_index < content.tmem_bytes.size() &&
                    content.tmem_bytes[tmem_index++] == tmem_[address];
        };
        const auto compare_tlut = [&](std::size_t index) {
            return index < palette_.size() &&
                    tlut_index < content.tlut_values.size() &&
                    content.tlut_values[tlut_index++] == palette_[index];
        };

        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t row = base + static_cast<std::size_t>(y) * source_stride;
                if (row >= tmem_.size()) continue;
                const std::size_t row_word_swap = (y & 1U) != 0U ? 4U : 0U;
                if (tile.size == 2 && row + static_cast<std::size_t>(x) * 2U + 1U < tmem_.size()) {
                    const std::size_t high_address = (row + static_cast<std::size_t>(x) * 2U) ^ row_word_swap;
                    const std::size_t low_address = (row + static_cast<std::size_t>(x) * 2U + 1U) ^ row_word_swap;
                    if (!compare_tmem(high_address) || !compare_tmem(low_address)) return false;
                    if (tile.format == 2) {
                        const std::uint16_t value = static_cast<std::uint16_t>(
                                (static_cast<std::uint16_t>(tmem_[high_address]) << 8U) |
                                tmem_[low_address]);
                        if (!compare_tlut(value & 0xFFU)) return false;
                    }
                } else if (tile.size == 1 && row + x < tmem_.size()) {
                    const std::size_t address = (row + x) ^ row_word_swap;
                    if (!compare_tmem(address)) return false;
                    if (tile.format == 2 && !compare_tlut(tmem_[address])) return false;
                } else if (tile.size == 0 && row + x / 2U < tmem_.size()) {
                    const std::size_t address = (row + x / 2U) ^ row_word_swap;
                    if (!compare_tmem(address)) return false;
                    if (tile.format == 2) {
                        const std::uint8_t packed = tmem_[address];
                        const std::uint8_t index = (x & 1U) == 0U ?
                                packed >> 4U : packed & 0x0FU;
                        if (!compare_tlut(index + tile.palette * 16U)) return false;
                    }
                }
            }
        }
        if (precombine_alpha) {
            const Tile &mask = tiles_[(key.tile_index + 1U) & 7U];
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::size_t address =
                            (std::size_t(mask.tmem) * 8U +
                                    y * mask.line * 8U + x / 2U) ^
                            ((y & 1U) != 0U ? 4U : 0U);
                    if (!compare_tmem(address)) return false;
                }
            }
        }
        return tmem_index == content.tmem_bytes.size() &&
                tlut_index == content.tlut_values.size();
    }

    bool shared_cache_entry_fits(const TextureDecodeContent &content,
            const N64RawFast3DTexture &texture) const {
        const std::size_t retained_bytes = sizeof(SharedTextureDecodeEntry) +
                content.tmem_bytes.capacity() +
                content.tlut_values.capacity() * sizeof(std::uint16_t) +
                texture.rgba.capacity();
        return retained_bytes <= kSharedTextureDecodeCacheMaxBytes;
    }

    void publish_shared_texture(const TextureDecodeCacheKey &key,
            TextureDecodeContent &&content,
            const std::shared_ptr<const N64RawFast3DTexture> &texture) {
        if (!shared_cache_entry_fits(content, *texture)) return;
        auto entry = std::make_shared<SharedTextureDecodeEntry>();
        entry->key = key;
        entry->content = std::move(content);
        entry->texture = texture;
        SharedTextureDecodeCacheSlot &slot = shared_texture_decode_cache();
        const std::lock_guard<std::mutex> guard(slot.mutex);
        slot.entry = std::move(entry); // One-entry replacement is the eviction policy.
    }

    bool shared_cache_candidate_fits(std::uint32_t width,
            std::uint32_t height, const Tile &tile,
            bool precombine_alpha) const {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        if (height != 0U && static_cast<std::size_t>(width) > maximum / height) return false;
        const std::size_t pixels = static_cast<std::size_t>(width) * height;
        std::size_t bytes_per_pixel = 4U; // RGBA output storage.
        if (tile.size == 2) bytes_per_pixel += 2U;
        else if (tile.size == 0 || tile.size == 1) bytes_per_pixel += 1U;
        if (tile.format == 2) bytes_per_pixel += 2U;
        if (precombine_alpha) ++bytes_per_pixel;
        const std::size_t overhead = sizeof(SharedTextureDecodeEntry);
        return overhead < kSharedTextureDecodeCacheMaxBytes &&
                pixels <= (kSharedTextureDecodeCacheMaxBytes - overhead) / bytes_per_pixel;
    }
    bool prepare_texture(std::uint8_t tile_index, bool rectangle = false) {
        const Tile &tile = tiles_[tile_index];
        const std::uint32_t width = std::max(1U, tile.width);
        const std::uint32_t height = std::max(1U, tile.height);
        const std::size_t source_stride = tile.line != 0 ?
                static_cast<std::size_t>(tile.line) * 8U :
                std::max<std::size_t>(1U, width * bytes_per_texel(tile.size));
        const std::size_t base = static_cast<std::size_t>(tile.tmem) * 8U;
        const TextureDecodeCacheKey cache_key =
                make_texture_cache_key(tile_index, tile, rectangle);
        const bool precombine_alpha = should_precombine_hud_alpha(
                tile_index, tile, width, height, rectangle);
        const bool shared_cacheable = shared_cache_candidate_fits(
                width, height, tile, precombine_alpha);
        std::shared_ptr<const N64RawFast3DTexture> texture_owner;
        bool texture_cache_hit = false;
        if (texture_cache_matches(cache_key)) {
            texture_owner = texture_decode_cache_.texture;
            texture_cache_hit = true;
            ++texture_cache_hits_;
        } else {
            if (shared_cacheable) {
                const auto candidate = shared_cache_snapshot();
                if (candidate && shared_texture_inputs_match(*candidate,
                            cache_key, tile, width, height, source_stride, base,
                            precombine_alpha)) {
                    texture_owner = candidate->texture;
                    texture_cache_hit = true;
                    ++shared_texture_cache_hits_;
                }
            }
            if (!texture_owner) {
                ++texture_cache_misses_;
                TextureDecodeContent captured_content;
                TextureDecodeContent *content_capture = shared_cacheable ?
                        &captured_content : nullptr;
                N64RawFast3DTexture decoded_texture;
                N64RawFast3DTexture &texture = decoded_texture;
            texture.width = width;
            texture.height = height;
            texture.format = tile.format;
            texture.size = tile.size;
            texture.wrap_s = n64_raw_texture_wrap(tile.cms,
                    (tile.descriptor_word1 >> 4U) & 15U, width);
            texture.wrap_t = n64_raw_texture_wrap(tile.cmt,
                    (tile.descriptor_word1 >> 14U) & 15U, height);
            const bool ia16_tlut = ((other_mode_ >> 46U) & 3U) == 3U;
            texture.rgba.resize(static_cast<std::size_t>(width) * height * 4U, 255);
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    std::uint32_t r = 255, g = 255, b = 255, a = 255;
                    const std::size_t row = base + static_cast<std::size_t>(y) * source_stride;
                    if (row >= tmem_.size()) continue;
                    const std::size_t row_word_swap = (y & 1U) != 0U ? 4U : 0U;
                    if (tile.size == 2 && row + static_cast<std::size_t>(x) * 2U + 1 < tmem_.size()) {
                        const std::size_t high_address = (row + x * 2U) ^ row_word_swap;
                        const std::size_t low_address = (row + x * 2U + 1U) ^ row_word_swap;
                        const std::uint8_t high_byte = tmem_[high_address];
                        const std::uint8_t low_byte = tmem_[low_address];
                        if (content_capture != nullptr) {
                            content_capture->tmem_bytes.push_back(high_byte);
                            content_capture->tmem_bytes.push_back(low_byte);
                        }
                        const std::uint16_t value = static_cast<std::uint16_t>(
                                (static_cast<std::uint16_t>(high_byte) << 8U) | low_byte);
                        if (tile.format == 2) {
                            const std::uint16_t palette_value = palette_[value & 0xFFU];
                            if (content_capture != nullptr) content_capture->tlut_values.push_back(palette_value);
                            if (ia16_tlut) {
                                r = g = b = palette_value >> 8U;
                                a = palette_value & 0xFFU;
                            } else {
                                r = expand5(palette_value >> 11U);
                                g = expand5((palette_value >> 6U) & 31U);
                                b = expand5((palette_value >> 1U) & 31U);
                                a = (palette_value & 1U) ? 255 : 0;
                            }
                        } else if (tile.format == 0) {
                            r = expand5(value >> 11U); g = expand5((value >> 6U) & 31U);
                            b = expand5((value >> 1U) & 31U); a = (value & 1U) ? 255 : 0;
                        } else {
                            r = g = b = (value >> 8U) & 0xFFU; a = value & 0xFFU;
                        }
                    } else if (tile.size == 1 && row + x < tmem_.size()) {
                        const std::size_t address = (row + x) ^ row_word_swap;
                        const std::uint8_t value = tmem_[address];
                        if (content_capture != nullptr) content_capture->tmem_bytes.push_back(value);
                        if (tile.format == 2) {
                            const std::uint16_t palette_value = palette_[value];
                            if (content_capture != nullptr) content_capture->tlut_values.push_back(palette_value);
                            if (ia16_tlut) {
                                r = g = b = palette_value >> 8U;
                                a = palette_value & 0xFFU;
                            } else {
                                r = expand5(palette_value >> 11U);
                                g = expand5((palette_value >> 6U) & 31U);
                                b = expand5((palette_value >> 1U) & 31U);
                                a = (palette_value & 1U) ? 255 : 0;
                            }
                        } else {
                            r = g = b = a = value;
                        }
                    } else if (tile.size == 0 && row + x / 2U < tmem_.size()) {
                        const std::size_t address = (row + x / 2U) ^ row_word_swap;
                        const std::uint8_t value = tmem_[address];
                        if (content_capture != nullptr) content_capture->tmem_bytes.push_back(value);
                        const std::uint8_t index = (x & 1U) == 0 ? value >> 4U : value & 0x0FU;
                        if (tile.format == 2) {
                            const std::uint16_t palette_value = palette_[index + tile.palette * 16U];
                            if (content_capture != nullptr) content_capture->tlut_values.push_back(palette_value);
                            if (ia16_tlut) {
                                r = g = b = palette_value >> 8U;
                                a = palette_value & 0xFFU;
                            } else {
                                r = expand5(palette_value >> 11U);
                                g = expand5((palette_value >> 6U) & 31U);
                                b = expand5((palette_value >> 1U) & 31U);
                                a = (palette_value & 1U) ? 255 : 0;
                            }
                        } else {
                            r = g = b = index * 17U;
                            // N64 I4 supplies intensity to every combiner channel,
                            // including alpha. Treating it as opaque produces the
                            // captured black timer box around an intensity mask.
                            a = tile.format == 4 ? r : 255;
                        }
                    }
                    const std::size_t out = (static_cast<std::size_t>(y) * width + x) * 4U;
                    texture.rgba[out + 0] = static_cast<std::uint8_t>(r);
                    texture.rgba[out + 1] = static_cast<std::uint8_t>(g);
                    texture.rgba[out + 2] = static_cast<std::uint8_t>(b);
                    texture.rgba[out + 3] = static_cast<std::uint8_t>(a);
                }
            }
            // Captured two-cycle HUD material: alpha = TEXEL1 * ENVIRONMENT.
            // Tile 1 is a same-coordinate I4 mask, loaded separately from RGBA16 RGB.
            const Tile &mask = tiles_[(tile_index + 1U) & 7U];
            if (precombine_alpha) {
                for (std::uint32_t y=0;y<height;++y) for (std::uint32_t x=0;x<width;++x) {
                    // Use the same physical TMEM representation as the color reader.
                    const auto address=(std::size_t(mask.tmem)*8U+y*mask.line*8U+x/2U)^((y&1U)?4U:0U);
                    const auto packed=tmem_[address];
                    if (content_capture != nullptr) content_capture->tmem_bytes.push_back(packed);
                    const unsigned intensity=((x&1U)?packed&15U:packed>>4U)*17U;
                    texture.rgba[(std::size_t(y)*width+x)*4U+3U]=
                            static_cast<std::uint8_t>((intensity*environment_color_.a+127U)/255U);
                }
                texture.alpha_precombined = true;
                if (diagnostic_task_selected()) std::fprintf(stderr,
                        "RW094_HUD_MASK tile=%u mask_tile=%u size=%ux%u environment_alpha=%u\n",
                        tile_index,(tile_index+1U)&7U,width,height,environment_color_.a);
            }
            texture_owner = std::make_shared<const N64RawFast3DTexture>(
                    std::move(decoded_texture));
            if (shared_cacheable) {
                publish_shared_texture(cache_key, std::move(captured_content),
                        texture_owner);
            }
            }
            remember_texture(cache_key, texture_owner);
        }
        const N64RawFast3DTexture &texture = *texture_owner;
        if (texture_cache_hit && texture.alpha_precombined && diagnostic_task_selected()) {
            std::fprintf(stderr,
                    "RW094_HUD_MASK tile=%u mask_tile=%u size=%ux%u environment_alpha=%u (cache hit)\n",
                    tile_index, (tile_index + 1U) & 7U, width, height,
                    environment_color_.a);
        }
        if (diagnostic_task_selected()) std::fprintf(stderr,
                "RW086_TILE_WRAP tile=%u descriptor=0x%08X cms=%u cmt=%u mask=%u,%u extent=%u,%u wrap=%u,%u origin=%u,%u\n",
                tile_index,tile.descriptor_word1,tile.cms,tile.cmt,(tile.descriptor_word1>>4U)&15U,
                (tile.descriptor_word1>>14U)&15U,width,height,unsigned(texture.wrap_s),unsigned(texture.wrap_t),tile.uls,tile.ult);
        const char *diagnostic_task_env = std::getenv("XR64_RW_DIAG_TASK");
        char *diagnostic_task_end = nullptr;
        const unsigned long long diagnostic_task = diagnostic_task_env != nullptr ?
                std::strtoull(diagnostic_task_env, &diagnostic_task_end, 10) : 0;
        const bool diagnostic_selected = diagnostic_task_env != nullptr &&
                diagnostic_task_end != diagnostic_task_env && *diagnostic_task_end == '\0' &&
                diagnostic_task == task_.sequence;
        if (diagnostic_selected && ::xr64::render_diagnostics::texture_capture() && !diagnostic_texture_captured_) {
            diagnostic_texture_captured_ = true;
            const std::size_t source_span = static_cast<std::size_t>(width) * height;
            std::size_t source_physical = 0;
            const bool source_valid = resolve(image_.address, source_physical) &&
                    valid_range(source_physical, source_span);
            std::fprintf(stderr,
                    "RW046_TEXTURE_DIAG task=%llu tile=%u format=%u size=%u "
                    "dimensions=%ux%u line=%u tmem=%u palette=%u "
                    "image_address=0x%08X source_physical=%s0x%08zX source_span=%zu "
                    "other_mode=0x%016llX combine=(0x%08X,0x%08X)\n",
                    static_cast<unsigned long long>(task_.sequence), tile_index,
                    tile.format, tile.size, width, height, tile.line, tile.tmem,
                    tile.palette, image_.address, source_valid ? "" : "invalid:",
                    source_physical, source_span,
                    static_cast<unsigned long long>(other_mode_), combine_word0_, combine_word1_);
            if (source_valid) {
                std::fprintf(stderr, "RW046_TEXTURE_SOURCE_HEAD task=%llu bytes=",
                        static_cast<unsigned long long>(task_.sequence));
                for (std::size_t index = 0; index < std::min<std::size_t>(32, source_span); ++index) {
                    std::fprintf(stderr, "%02X", byte_at(source_physical + index));
                }
                std::fprintf(stderr, "\nRW046_TLUT_HEAD task=%llu rgba16=",
                        static_cast<unsigned long long>(task_.sequence));
                for (std::size_t index = 0; index < 16; ++index) {
                    std::fprintf(stderr, "%04X", palette_[index]);
                }
                std::fprintf(stderr, "\n");
                const char *capture_dir_env = std::getenv("XR64_RW_CAPTURE_DIR");
                if (capture_dir_env != nullptr && capture_dir_env[0] != '\0') {
                    std::error_code directory_error;
                    const std::filesystem::path capture_dir(capture_dir_env);
                    std::filesystem::create_directories(capture_dir, directory_error);
                    if (!directory_error) {
                        const std::string stem = "task-" + std::to_string(task_.sequence);
                        std::ofstream source_output(capture_dir / (stem + "-texture-source.bin"),
                                std::ios::binary);
                        for (std::size_t index = 0; source_output && index < source_span; ++index) {
                            const char byte = static_cast<char>(byte_at(source_physical + index));
                            source_output.write(&byte, 1);
                        }
                        std::ofstream palette_output(capture_dir / (stem + "-palette-rgba16.bin"),
                                std::ios::binary);
                        for (const std::uint16_t value : palette_) {
                            const std::array<char, 2> bytes{{
                                static_cast<char>(value >> 8U), static_cast<char>(value)}};
                            palette_output.write(bytes.data(), bytes.size());
                        }
                    }
                }
            }
            XR64_RENDER_DIAGNOSTIC_FLUSH();
        }
        if (diagnostic_task_selected() && ::xr64::render_diagnostics::texture_capture()) {
            const char *directory = std::getenv("XR64_RW_CAPTURE_DIR");
            if (directory && *directory) {
                const auto stem = std::filesystem::path(directory) /
                    ("tmem-" + std::to_string(task_.sequence) + "-" + std::to_string(++diagnostic_upload_));
                std::filesystem::create_directories(directory);
                auto dump = [&](const char *suffix, const void *data, std::size_t bytes) {
                    std::ofstream out(stem.string() + suffix, std::ios::binary);
                    out.write(static_cast<const char *>(data), bytes);
                };
                dump("-tmem.bin", tmem_.data(), tmem_.size());
                dump("-rgba.bin", texture.rgba.data(), texture.rgba.size());
                std::vector<std::uint8_t> source(source_stride * height);
                for (std::size_t i = 0; i < source.size(); ++i) {
                    std::size_t physical = 0;
                    if (resolve(image_.address + static_cast<std::uint32_t>(i), physical) && valid_range(physical, 1))
                        source[i] = byte_at(physical);
                }
                dump("-source.bin", source.data(), source.size());
                std::ofstream meta(stem.string() + ".txt");
                meta << width << " " << height << " " << unsigned(tile.format) << " " << unsigned(tile.size)
                     << " " << source_stride << " " << base << " " << image_.address << "\n";
            }
        }
        return suppress_replaced_ || backend_.upload_texture(texture, error_);
    }

    bool copy_to_tmem(std::uint8_t tile_index, std::uint32_t upper_s,
            std::uint32_t upper_t, std::uint32_t lower_s, std::uint32_t lower_t) {
        const Tile &tile = tiles_[tile_index];
        const std::size_t source_bpt = bytes_per_texel(image_.size);
        if (source_bpt == 0) { error_ = "raw_fast3d_invalid_texture_size"; return false; }
        const std::size_t source_stride = std::max<std::size_t>(1U,
                static_cast<std::size_t>(image_.width) * source_bpt);
        const std::size_t destination = static_cast<std::size_t>(tile.tmem) * 8U;
        const std::size_t destination_stride = tile.line != 0 ?
                static_cast<std::size_t>(tile.line) * 8U :
                std::max<std::size_t>(1U, (lower_s - upper_s + 1U) * source_bpt);
        for (std::uint32_t y = upper_t; y <= lower_t; ++y) {
            for (std::uint32_t x = upper_s; x <= lower_s; ++x) {
                const std::size_t source_offset = static_cast<std::size_t>(y) * source_stride +
                        static_cast<std::size_t>(x) * source_bpt;
                const std::size_t destination_offset = destination +
                        static_cast<std::size_t>(y - upper_t) * destination_stride +
                        static_cast<std::size_t>(x - upper_s) * source_bpt;
                if (!valid_guest_offset(image_.address, source_offset, source_bpt) ||
                        destination_offset + source_bpt > tmem_.size()) {
                    error_ = "raw_fast3d_texture_load_outside_rdram";
                    return false;
                }
                std::size_t physical = 0;
                if (!resolve(image_.address + static_cast<std::uint32_t>(source_offset), physical)) return false;
                for (std::size_t byte = 0; byte < source_bpt; ++byte)
                    tmem_[(destination_offset + byte) ^ (((y - upper_t) & 1U) ? 4U : 0U)] =
                            byte_at(physical + byte);
            }
        }
        ++tmem_revision_;
        return true;
    }

    bool load_block_to_tmem(std::uint8_t tile_index, std::uint32_t upper_s,
            std::uint32_t upper_t, std::uint32_t lower_s, std::uint32_t dxt) {
        const Tile &tile = tiles_[tile_index];
        const std::size_t bpt = bytes_per_texel(image_.size);
        if (bpt == 0 || lower_s < upper_s) {
            error_ = "raw_fast3d_invalid_texture_size";
            return false;
        }
        const std::size_t bytes = (lower_s - upper_s + 1U) * bpt;
        const std::size_t source_start = (static_cast<std::size_t>(upper_t) * image_.width + upper_s) * bpt;
        const std::size_t base = static_cast<std::size_t>(tile.tmem) * 8U;
        // DxT is 1.11 lines per transferred 64-bit word. For LoadBlock,
        // tile.line is an additional word skip per line, not a row pitch.
        // DxT=0 deliberately leaves pre-interleaved source bytes untouched.
        for (std::size_t offset = 0; offset < bytes; ++offset) {
            const std::size_t t = ((offset / 8U) * dxt) >> 11U;
            const std::size_t destination = (base + offset + t * tile.line * 8U) ^ ((t & 1U) ? 4U : 0U);
            if (destination >= tmem_.size() || !valid_guest_offset(image_.address, source_start + offset, 1)) {
                error_ = "raw_fast3d_texture_load_outside_rdram";
                return false;
            }
            std::size_t physical = 0;
            if (!resolve(image_.address + static_cast<std::uint32_t>(source_start + offset), physical)) return false;
            tmem_[destination] = byte_at(physical);
        }
        ++tmem_revision_;
        return true;
    }

    bool valid_guest_offset(std::uint32_t address, std::size_t offset, std::size_t size) {
        std::size_t physical = 0;
        if (!resolve(address + static_cast<std::uint32_t>(offset), physical)) return false;
        return valid_range(physical, size);
    }

    bool draw_triangle(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
        if (a >= kMaxVertices || b >= kMaxVertices || c >= kMaxVertices ||
                !vertex_loaded_[a] || !vertex_loaded_[b] || !vertex_loaded_[c]) {
            error_ = "raw_fast3d_triangle_references_unloaded_vertex";
            return false;
        }
        std::vector<N64RawFast3DVertex> triangle = {vertices_[a], vertices_[b], vertices_[c]};
        const Tile &tile = tiles_[active_tile_];
        if (texture_enabled_ && !prepare_texture(active_tile_)) return false;
        for (N64RawFast3DVertex &vertex : triangle) {
            if (diagnostic_task_selected()) std::fprintf(stderr,
                    "RW081_UV_INPUT draw=%u tile=%u dimensions=%ux%u shift=%u,%u mask=%u,%u scale=%u,%u st=%.6f,%.6f\n",
                    stats_ ? stats_->triangle_count + 1 : 0, active_tile_, tile.width, tile.height,
                    tile.descriptor_word1 & 15U, (tile.descriptor_word1 >> 10U) & 15U, (tile.descriptor_word1 >> 4U) & 15U, (tile.descriptor_word1 >> 14U) & 15U, texture_scale_s_, texture_scale_t_, vertex.s, vertex.t);
            vertex.s *= static_cast<float>(texture_scale_s_) / 65536.0F;
            vertex.t *= static_cast<float>(texture_scale_t_) / 65536.0F;
            if (tile.width != 0) vertex.s /= static_cast<float>(tile.width);
            if (tile.height != 0) vertex.t /= static_cast<float>(tile.height);
        }
        const bool submitted = suppress_replaced_ || backend_.draw_triangles(triangle, draw_state(), error_);
        #if XR64_RENDER_DIAGNOSTICS
        if (submitted && stats_ != nullptr) ++stats_->triangle_count;
#endif
        return submitted;
    }

    bool modify_vertex(std::uint32_t word0, std::uint32_t word1) {
        const std::uint32_t encoded_index = word0 & 0xFFFFU;
        if ((encoded_index & 1U) != 0U) {
            error_ = "raw_fast3d_f3dex2_modify_vertex_unaligned_index";
            return false;
        }
        const std::uint32_t index = encoded_index >> 1U;
        if (index >= vertices_.size() || !vertex_loaded_[index]) {
            error_ = "raw_fast3d_f3dex2_modify_vertex_unloaded_vertex";
            return false;
        }
        N64RawFast3DVertex &vertex = vertices_[index];
        switch (static_cast<std::uint8_t>((word0 >> 16U) & 0xFFU)) {
            case kF3dex2ModifyVtxRgba:
                set_color(word1, vertex.color);
                return true;
            case kF3dex2ModifyVtxSt:
                vertex.s = static_cast<float>(static_cast<std::int16_t>(word1 >> 16U)) / 32.0F;
                vertex.t = static_cast<float>(static_cast<std::int16_t>(word1)) / 32.0F;
                return true;
            case kF3dex2ModifyVtxXyScreen:
                if (viewport_scale_x_ == 0 || viewport_scale_y_ == 0) {
                    error_ = "raw_fast3d_f3dex2_modify_vertex_invalid_xy_viewport";
                    return false;
                }
                vertex.x = (static_cast<float>(static_cast<std::int16_t>(word1 >> 16U)) -
                        static_cast<float>(viewport_translate_x_)) /
                        static_cast<float>(viewport_scale_x_) * vertex.w;
                vertex.y = (static_cast<float>(static_cast<std::int16_t>(word1)) -
                        static_cast<float>(viewport_translate_y_)) /
                        static_cast<float>(viewport_scale_y_) * vertex.w;
                return true;
            case kF3dex2ModifyVtxZScreen:
                if (viewport_scale_z_ == 0) {
                    error_ = "raw_fast3d_f3dex2_modify_vertex_invalid_z_viewport";
                    return false;
                }
                vertex.z = (static_cast<float>(word1) / 65536.0F -
                        static_cast<float>(viewport_translate_z_)) /
                        static_cast<float>(viewport_scale_z_) * vertex.w;
                return true;
            default:
                error_ = "raw_fast3d_f3dex2_modify_vertex_unsupported_field";
                return false;
        }
    }

    void set_color(std::uint32_t packed, N64RawFast3DColor &color) {
        color.r = static_cast<std::uint8_t>(packed >> 24U);
        color.g = static_cast<std::uint8_t>(packed >> 16U);
        color.b = static_cast<std::uint8_t>(packed >> 8U);
        color.a = static_cast<std::uint8_t>(packed);
    }

    bool execute_list(std::uint32_t address, std::size_t depth) {
        std::size_t physical=0;
        if (!suppress_replaced_ && replacement_ && replacement_->physical_address &&
                resolve(address,physical) && physical==replacement_->physical_address) {
            if(replacement_->batches.empty()) {
                const bool previous=camera_attachment_;
                camera_attachment_=replacement_->camera_attachment;
                const bool ok=execute_list_commands(address,depth);
                camera_attachment_=previous;
                replacement_->matched=true;
                return ok;
            }
            suppress_replaced_=true;
            const bool ok=execute_list_commands(address,depth);
            suppress_replaced_=false;
            if (!ok) return false;
            replacement_->matched=true;
            for (const auto& batch:replacement_->batches) {
                auto state=draw_state();
                state.camera_attachment=replacement_->camera_attachment;
                state.tracked_attachment=replacement_->tracked_attachment;
                state.textured=true;state.cull_front=false;state.cull_back=false;
                state.combine_word0=batch.combine_word0;state.combine_word1=batch.combine_word1;
                state.other_mode=(state.other_mode & ~(3ULL<<52U)) | (1ULL<<52U);
                state.primitive_color={255,255,255,255};state.environment_color={255,255,255,255};
                if (!backend_.upload_texture(batch.texture,error_) ||
                    !backend_.draw_triangles(batch.vertices,state,error_)) return false;
            }
            return true;
        }
        return execute_list_commands(address,depth);
    }
    bool execute_list_commands(std::uint32_t address, std::size_t depth) {
        if (depth > kMaxDepth) { error_ = "raw_fast3d_display_list_recursion_limit"; return false; }
        std::size_t cursor = 0;
        if (!resolve(address, cursor) || !valid_range(cursor, 8)) {
            error_ = "raw_fast3d_display_list_outside_rdram";
            return false;
        }
        while (valid_range(cursor, 8)) {
            if (++command_count_ > kMaxCommands) { error_ = "raw_fast3d_command_limit"; return false; }
#if XR64_RENDER_DIAGNOSTICS
            if (stats_ != nullptr) stats_->command_count = command_count_;
#endif
            const std::uint32_t w0 = (static_cast<std::uint32_t>(byte_at(cursor)) << 24U) |
                    (static_cast<std::uint32_t>(byte_at(cursor + 1)) << 16U) |
                    (static_cast<std::uint32_t>(byte_at(cursor + 2)) << 8U) |
                    static_cast<std::uint32_t>(byte_at(cursor + 3));
            const std::uint32_t w1 = (static_cast<std::uint32_t>(byte_at(cursor + 4)) << 24U) |
                    (static_cast<std::uint32_t>(byte_at(cursor + 5)) << 16U) |
                    (static_cast<std::uint32_t>(byte_at(cursor + 6)) << 8U) |
                    static_cast<std::uint32_t>(byte_at(cursor + 7));
            cursor += 8;
            const std::uint8_t opcode = static_cast<std::uint8_t>(w0 >> 24U);
            if (std::getenv("XR64_RW022_TRACE_COMMANDS") != nullptr) {
                if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr,
                        "RW020_RAW_GBI_COMMAND physical=0x%08zX opcode=0x%02X "
                        "w0=0x%08X w1=0x%08X depth=%zu\n",
                        cursor - 8U, static_cast<unsigned>(opcode), w0, w1, depth);
                XR64_RENDER_DIAGNOSTIC_FLUSH();
            }
            if (task_.microcode_family == N64MicrocodeFamily::F3DEX2) {
                if (opcode == kF3dex2Noop) continue;
                if (opcode == kF3dex2Vtx) {
                    const std::uint32_t count = (w0 >> 12U) & 0xFFU;
                    const std::uint32_t end_times_two = (w0 >> 1U) & 0x7FU;
                    if (count == 0 || end_times_two < count) { error_ = "raw_fast3d_invalid_f3dex2_vertex_command"; return false; }
                    const std::uint32_t first = end_times_two - count;
                    for (std::uint32_t index = 0; index < count; ++index) {
                        const std::uint32_t vertex_address = w1 + index * 16U;
                        if (first + index >= kMaxVertices) {
                            std::fprintf(stderr,
                                    "RW045_VERTEX_REJECT command_physical=0x%08zX w0=0x%08X "
                                    "w1=0x%08X count=%u first=%u index=%u address=0x%08X "
                                    "reason=vertex_slot_out_of_range\n",
                                    cursor - 8U, w0, w1, count, first, index,
                                    vertex_address);
                            XR64_RENDER_DIAGNOSTIC_FLUSH();
                            error_ = "raw_fast3d_vertex_load_failed";
                            return false;
                        }
                        if (!read_vertex(vertex_address, vertices_[first + index])) {
                            const std::string detail = error_.empty() ?
                                    "vertex_memory_read_failed" : error_;
                            std::fprintf(stderr,
                                    "RW045_VERTEX_REJECT command_physical=0x%08zX w0=0x%08X "
                                    "w1=0x%08X count=%u first=%u index=%u address=0x%08X "
                                    "reason=%s\n",
                                    cursor - 8U, w0, w1, count, first, index,
                                    vertex_address, detail.c_str());
                            XR64_RENDER_DIAGNOSTIC_FLUSH();
                            error_ = "raw_fast3d_vertex_load_failed";
                            return false;
                        }
                        vertex_loaded_[first + index] = true;
                    }
                    continue;
                }
                if (opcode == kF3dex2Tri1) { if (!draw_triangle(static_cast<std::uint8_t>((w0 >> 16U) & 0xFFU) / 2U, static_cast<std::uint8_t>((w0 >> 8U) & 0xFFU) / 2U, static_cast<std::uint8_t>(w0 & 0xFFU) / 2U)) return false; continue; }
                if (opcode == kF3dex2Tri2 || opcode == kF3dex2Quad) {
                    if (!draw_triangle(static_cast<std::uint8_t>((w0 >> 16U) & 0xFFU) / 2U, static_cast<std::uint8_t>((w0 >> 8U) & 0xFFU) / 2U, static_cast<std::uint8_t>(w0 & 0xFFU) / 2U) ||
                            !draw_triangle(static_cast<std::uint8_t>((w1 >> 16U) & 0xFFU) / 2U, static_cast<std::uint8_t>((w1 >> 8U) & 0xFFU) / 2U, static_cast<std::uint8_t>(w1 & 0xFFU) / 2U)) return false;
                    continue;
                }
                if (opcode == kF3dex2Geom) {
                    const std::uint32_t before = geometry_mode_;
                    geometry_mode_ = (geometry_mode_ & (w0 & 0x00FFFFFFU)) | w1;
                    if (diagnostic_task_selected()) {
                        std::fprintf(stderr, "RW079_GEOMETRY_MODE task=%llu before=0x%08X clear_mask=0x%06X set_mask=0x%08X after=0x%08X\n",
                                static_cast<unsigned long long>(task_.sequence), before,
                                w0 & 0x00FFFFFFU, w1, geometry_mode_);
                        XR64_RENDER_DIAGNOSTIC_FLUSH();
                    }
                    continue;
                }
                if (opcode == kF3dex2Mtx) { if (!apply_matrix(w0, w1)) return false; continue; }
                if (opcode == kF3dex2PopMtx) { const std::size_t count = w1 / 64U; if (count == 0 || w1 % 64U != 0) { error_ = "raw_fast3d_invalid_f3dex2_pop_matrix"; return false; } modelview_depth_ = count > modelview_depth_ ? 0 : modelview_depth_ - count; update_mp(); continue; }
                if (opcode == kF3dex2MoveWord) {
                    const std::uint8_t subtype =
                            static_cast<std::uint8_t>((w0 >> 16U) & 0xFFU);
                    const std::uint16_t offset =
                            static_cast<std::uint16_t>(w0 & 0xFFFFU);
                    if (subtype == 2U) {
                        num_lights_ = std::min<std::size_t>(w1 / 24U, kMaxDirectionalLights);
                    }
                    if (diagnostic_task_selected() && (subtype == 2U || subtype == 10U)) {
                        std::fprintf(stderr, "RW079_MOVEWORD task=%llu subtype=0x%02X offset=0x%04X value=0x%08X%s\n",
                                static_cast<unsigned long long>(task_.sequence), subtype, offset, w1,
                                subtype == 2U ? " num_lights=value/24" : " light_color");
                        XR64_RENDER_DIAGNOSTIC_FLUSH();
                    }
                    if (subtype == 6U) {
                        if ((offset & 3U) != 0U) {
                            error_ = "raw_fast3d_unaligned_segment_offset";
                            return false;
                        }
                        const std::size_t index = offset >> 2U;
                        if (index >= segments_.size()) {
                            error_ = "raw_fast3d_invalid_segment_index";
                            return false;
                        }
                        segments_[index] = w1 & 0x00FFFFFFU;
                        segment_loaded_[index] = true;
                        if (std::getenv("XR64_RW022_TRACE_COMMANDS") != nullptr) {
                            std::fprintf(stderr,
                                    "RW045_SEGMENT_SET command_physical=0x%08zX "
                                    "microcode=F3DEX2 subtype=0x%02X offset=0x%04X "
                                    "segment=%zu base=0x%08X\n",
                                    cursor - 8U, subtype, offset, index,
                                    segments_[index]);
                            XR64_RENDER_DIAGNOSTIC_FLUSH();
                        }
                    }
                    continue;
                }
                if (opcode == kF3dex2MoveMem) {
                    trace_light_move_mem(w0, w1);
                    if (!load_standard_f3dex2_light(w0, w1)) return false;
                    if ((w0 & 0xFFU) == 8U) { std::int16_t sx=0,sy=0,sz=0,tx=0,ty=0,tz=0; if (!read_i16(w1, sx) || !read_i16(w1+2, sy) || !read_i16(w1+4, sz) || !read_i16(w1+8, tx) || !read_i16(w1+10, ty) || !read_i16(w1+12, tz)) { error_ = "raw_fast3d_viewport_load_failed"; return false; } viewport_scale_x_=sx; viewport_scale_y_=sy; viewport_scale_z_=sz; viewport_translate_x_=tx; viewport_translate_y_=ty; viewport_translate_z_=tz; viewport_width_ = std::max(1, static_cast<int>(std::abs(sx) / 2)); viewport_height_ = std::max(1, static_cast<int>(std::abs(sy) / 2)); if (!backend_.set_viewport((static_cast<int>(tx) - std::abs(sx)) / 4, (static_cast<int>(ty) - std::abs(sy)) / 4, viewport_width_, viewport_height_, error_)) return false; }
                    continue;
                }
                if (opcode == kF3dex2Texture) { texture_enabled_ = ((w0 >> 1U) & 0x7FU) != 0; active_tile_ = static_cast<std::uint8_t>((w0 >> 8U) & 7U); texture_scale_s_ = static_cast<std::uint16_t>(w1 >> 16U); texture_scale_t_ = static_cast<std::uint16_t>(w1); continue; }
                if (opcode == kF3dex2ModeL || opcode == kF3dex2ModeH) { apply_mode(w0, w1, opcode == kF3dex2ModeH); continue; }
                if (opcode == kF3dex2Dl) { if (!execute_list(w1, depth + 1)) return false; if (((w0 >> 16U) & 1U) != 0) return true; continue; }
                if (opcode == kF3dex2End) return true;
                if (opcode == kF3dex2Half1 || opcode == kF3dex2Half2) continue;
                if (opcode == kF3dex2ModifyVtx) { if (!modify_vertex(w0, w1)) return false; continue; }
            } else {
                if (opcode == kF3dNoop) continue;
                if (opcode == kF3dDl) { if (!execute_list(w1, depth + 1)) return false; if (((w0 >> 16U) & 1U) != 0) return true; continue; }
                if (opcode == kF3dEnd) return true;
                if (opcode == kF3dMtx) { if (!apply_matrix(w0, w1)) return false; continue; }
                if (opcode == kF3dPopMtx) { if (modelview_depth_ > 0) --modelview_depth_; update_mp(); continue; }
                if (opcode == kF3dMoveWord) { if ((w0 >> 16U & 0xFFU) == 6U) { const std::size_t index = (w0 >> 8U) & 0xFFU; if (index >= segments_.size()) { error_ = "raw_fast3d_invalid_segment_index"; return false; } segments_[index] = w1 & 0x00FFFFFFU; segment_loaded_[index] = true; } continue; }
                if (opcode == kF3dSetGeom) { geometry_mode_ |= w1; continue; }
                if (opcode == kF3dClearGeom) { geometry_mode_ &= ~w1; continue; }
                if (opcode == kF3dModeL || opcode == kF3dModeH) { apply_mode(w0, w1, opcode == kF3dModeH); continue; }
                if (opcode == kF3dTexture) { texture_enabled_ = (w0 & 0xFFU) != 0; active_tile_ = static_cast<std::uint8_t>((w0 >> 8U) & 7U); continue; }
                if (opcode == kF3dRdpHalf1 || opcode == kF3dRdpHalf2) continue;
            }
            if (!execute_rdp(opcode, w0, w1, cursor)) return false;
        }
        error_ = "raw_fast3d_display_list_did_not_terminate";
        return false;
    }

    void apply_mode(std::uint32_t w0, std::uint32_t w1, bool high) {
        std::uint32_t shift = (w0 >> 8U) & 0xFFU;
        std::uint32_t length = w0 & 0xFFU;
        if (task_.microcode_family == N64MicrocodeFamily::F3DEX2) {
            ++length;
            if (shift + length > 32U) return;
            shift = 32U - shift - length;
        }
        if (shift >= 32U || length > 32U - shift) return;
        const std::uint32_t mask = length == 32U ? 0xFFFFFFFFU : ((1U << length) - 1U) << shift;
        std::uint32_t &word = high ? reinterpret_cast<std::uint32_t *>(&other_mode_)[1] : reinterpret_cast<std::uint32_t *>(&other_mode_)[0];
        word = (word & ~mask) | (w1 & mask);
    }

    bool execute_rdp(std::uint8_t opcode, std::uint32_t w0, std::uint32_t w1, std::size_t &cursor) {
        switch (opcode) {
            case kSetCImg: {
                const std::uint32_t width = (w0 & 0x7FFU) + 1U;
                if (!backend_.set_color_image(w1, width, error_)) return false;
#if XR64_RENDER_DIAGNOSTICS
                if (stats_ != nullptr) { stats_->color_image_set = true; stats_->color_image_address = w1; stats_->color_image_width = width; stats_->color_image_format = (w0 >> 21U) & 7U; stats_->color_image_size = (w0 >> 19U) & 3U; }
#endif
                viewport_width_ = static_cast<int>(width);
                return true;
            }
            case kSetZImg: return backend_.set_depth_image(w1, error_);
            case kSetTImg: image_.format = (w0 >> 21U) & 7U; image_.size = (w0 >> 19U) & 3U; image_.width = (w0 & 0x0FFFU) + 1U; image_.address = w1; if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW044_SET_TIMG format=%u size=%u width=%u address=0x%08X\n", image_.format, image_.size, image_.width, image_.address); XR64_RENDER_DIAGNOSTIC_FLUSH(); return true;
            case kSetCombine: combine_word0_ = w0; combine_word1_ = w1; return true;
            case kSetEnv: set_color(w1, environment_color_); return true;
            case kSetPrim: set_color(w1, primitive_color_); return true;
            case kSetBlend: return true;
            case kSetFog: return true;
            case kSetFill: { const std::uint16_t value = static_cast<std::uint16_t>(w1); fill_color_ = {expand5(value >> 11U), expand5((value >> 6U) & 31U), expand5((value >> 1U) & 31U), static_cast<std::uint8_t>((value & 1U) ? 255 : 0)}; if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW021_RDP_SET_FILL raw_w1=0x%08X decoded_rgba=(%u,%u,%u,%u)\n", w1, static_cast<unsigned>(fill_color_.r), static_cast<unsigned>(fill_color_.g), static_cast<unsigned>(fill_color_.b), static_cast<unsigned>(fill_color_.a)); XR64_RENDER_DIAGNOSTIC_FLUSH(); return true; }
            case kSetTile: { const std::uint8_t tile = static_cast<std::uint8_t>((w1 >> 24U) & 7U); Tile &state = tiles_[tile]; state.descriptor_word1 = w1; state.format = (w0 >> 21U) & 7U; state.size = (w0 >> 19U) & 3U; state.line = (w0 >> 9U) & 0x1FFU; state.tmem = w0 & 0x1FFU; state.palette = (w1 >> 20U) & 0xFU; state.cmt = (w1 >> 18U) & 3U; state.cms = (w1 >> 8U) & 3U; if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW044_SET_TILE tile=%u format=%u size=%u line=%u tmem=%u palette=%u\n", tile, state.format, state.size, state.line, state.tmem, state.palette); XR64_RENDER_DIAGNOSTIC_FLUSH(); return true; }
            case kSetTileSize: { const std::uint8_t tile = static_cast<std::uint8_t>((w1 >> 24U) & 7U); Tile &state = tiles_[tile]; state.uls = (w0 >> 12U) & 0xFFFU; state.ult = w0 & 0xFFFU; state.lrs = (w1 >> 12U) & 0xFFFU; state.lrt = w1 & 0xFFFU; state.width = (state.lrs - state.uls + 4U) / 4U; state.height = (state.lrt - state.ult + 4U) / 4U; return true; }
            case kLoadBlock: {
                const auto tile = static_cast<std::uint8_t>((w1 >> 24U) & 7U);
                // LoadBlock coordinates are integers, unlike LoadTile's 10.2.
                const std::uint32_t upper_s = (w0 >> 12U) & 0xFFFU;
                const std::uint32_t upper_t = w0 & 0xFFFU;
                const std::uint32_t lower_s = (w1 >> 12U) & 0xFFFU;
                const std::uint32_t dxt = w1 & 0xFFFU;
                if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW044_LOAD_BLOCK tile=%u uls=%u ult=%u lrs=%u dxt=%u image_format=%u image_size=%u image_width=%u\n",
                        tile, upper_s, upper_t, lower_s, dxt, image_.format, image_.size, image_.width);
                XR64_RENDER_DIAGNOSTIC_FLUSH();
                return load_block_to_tmem(tile, upper_s, upper_t, lower_s, dxt);
            }
            case kLoadTile: { const std::uint8_t tile = static_cast<std::uint8_t>((w1 >> 24U) & 7U); return copy_to_tmem(tile, ((w0 >> 12U) & 0xFFFU) / 4U, (w0 & 0xFFFU) / 4U, ((w1 >> 12U) & 0xFFFU) / 4U, (w1 & 0xFFFU) / 4U); }
            case kLoadTlut: {
                const std::uint8_t tile_index = static_cast<std::uint8_t>((w1 >> 24U) & 7U);
                const std::uint32_t upper_s = (w0 >> 14U) & 0x3FFU;
                const std::uint32_t upper_t = (w0 >> 2U) & 0x3FFU;
                const std::uint32_t lower_s = (w1 >> 14U) & 0x3FFU;
                const std::uint32_t lower_t = (w1 >> 2U) & 0x3FFU;
                const std::uint32_t count = (lower_s >= upper_s && lower_t >= upper_t) ?
                        (lower_s - upper_s + 1U) * (lower_t - upper_t + 1U) : 0U;
                const std::size_t palette_base = static_cast<std::size_t>(tiles_[tile_index].tmem >= 256 ?
                        tiles_[tile_index].tmem - 256 : 0);
                if (count == 0 || palette_base + count > palette_.size()) {
                    error_ = "raw_fast3d_invalid_tlut_bounds";
                    return false;
                }
                if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW044_LOAD_TLUT tile=%u count=%u palette_base=%zu image_address=0x%08X image_width=%u\n", tile_index, count, palette_base, image_.address, image_.width);
                XR64_RENDER_DIAGNOSTIC_FLUSH();
                for (std::uint32_t index = 0; index < count; ++index) {
                    std::uint16_t value = 0;
                    if (!read_u16(image_.address + (upper_t * image_.width + upper_s + index) * 2U, value)) {
                        error_ = "raw_fast3d_tlut_load_outside_rdram";
                        return false;
                    }
                    palette_[palette_base + index] = value;
                }
                ++palette_revision_;
                return true;
            }
            case kScissor: {
                const int x = static_cast<int>(((w0 >> 12U) & 0xFFFU) / 4U);
                const int y = static_cast<int>((w0 & 0xFFFU) / 4U);
                const int width = static_cast<int>(((w1 >> 12U) & 0xFFFU) / 4U) - x;
                const int height = static_cast<int>((w1 & 0xFFFU) / 4U) - y;
                if (!backend_.set_scissor(x, y, width, height, error_)) return false;
#if XR64_RENDER_DIAGNOSTICS
                if (stats_ != nullptr) { stats_->scissor_set = true; stats_->scissor_x = x; stats_->scissor_y = y; stats_->scissor_width = width; stats_->scissor_height = height; }
#endif
                return true;
            }
            case kRdpMode: other_mode_ = (static_cast<std::uint64_t>(w0 & 0x00FFFFFFU) << 32U) | w1; return true;
            case kTexRect:
            case kTexRectFlip: {
                if (!valid_range(cursor, 16)) { error_ = "raw_fast3d_texture_rectangle_missing_words"; return false; }
                const std::uint32_t w2 = (static_cast<std::uint32_t>(byte_at(cursor)) << 24U) | (static_cast<std::uint32_t>(byte_at(cursor + 1)) << 16U) | (static_cast<std::uint32_t>(byte_at(cursor + 2)) << 8U) | byte_at(cursor + 3);
                const std::uint32_t w3 = (static_cast<std::uint32_t>(byte_at(cursor + 4)) << 24U) | (static_cast<std::uint32_t>(byte_at(cursor + 5)) << 16U) | (static_cast<std::uint32_t>(byte_at(cursor + 6)) << 8U) | byte_at(cursor + 7);
                const std::uint32_t w4 = (static_cast<std::uint32_t>(byte_at(cursor + 8)) << 24U) | (static_cast<std::uint32_t>(byte_at(cursor + 9)) << 16U) | (static_cast<std::uint32_t>(byte_at(cursor + 10)) << 8U) | byte_at(cursor + 11);
                const std::uint32_t w5 = (static_cast<std::uint32_t>(byte_at(cursor + 12)) << 24U) | (static_cast<std::uint32_t>(byte_at(cursor + 13)) << 16U) | (static_cast<std::uint32_t>(byte_at(cursor + 14)) << 8U) | byte_at(cursor + 15);
                cursor += 16;
                const int left = static_cast<int>(((w1 >> 12U) & 0xFFFU) / 4U);
                const int top = static_cast<int>((w1 & 0xFFFU) / 4U);
                const int right = static_cast<int>(((w0 >> 12U) & 0xFFFU) / 4U);
                const int bottom = static_cast<int>((w0 & 0xFFFU) / 4U);
                const std::uint8_t tile = static_cast<std::uint8_t>((w1 >> 24U) & 7U);
                if (texture_enabled_ && !prepare_texture(tile, true)) return false;
                const Tile &state = tiles_[tile];
                const float texture_width = static_cast<float>(std::max(1U, state.width));
                const float texture_height = static_cast<float>(std::max(1U, state.height));
                const float start_s = static_cast<float>(static_cast<std::int16_t>(w3 >> 16U)) / 32.0F;
                const float start_t = static_cast<float>(static_cast<std::int16_t>(w3)) / 32.0F;
                const float dsdx = static_cast<float>(static_cast<std::int16_t>(w5 >> 16U)) / 1024.0F;
                const float dtdy = static_cast<float>(static_cast<std::int16_t>(w5)) / 1024.0F;
                const float s0 = start_s / texture_width;
                const float t0 = start_t / texture_height;
                const float s1 = (start_s + dsdx * static_cast<float>(right - left)) / texture_width;
                const float t1 = (start_t + dtdy * static_cast<float>(bottom - top)) / texture_height;
                (void)w2;
                (void)w4;
                const bool submitted = backend_.draw_rectangle(static_cast<float>(left),
                        static_cast<float>(top), static_cast<float>(right),
                        static_cast<float>(bottom), s0, t0, s1, t1,
                        draw_state(), error_);
                #if XR64_RENDER_DIAGNOSTICS
                if (submitted && stats_ != nullptr) ++stats_->rectangle_count;
#endif
                return submitted;
            }
            case kFillRect: {
                const float left = static_cast<float>(((w1 >> 12U) & 0xFFFU) / 4U);
                const float top = static_cast<float>((w1 & 0xFFFU) / 4U);
                const float right = static_cast<float>(((w0 >> 12U) & 0xFFFU) / 4U);
                const float bottom = static_cast<float>((w0 & 0xFFFU) / 4U);
#if XR64_RENDER_DIAGNOSTICS
                if (::xr64::render_diagnostics::legacy() || diagnostic_task_selected()) std::fprintf(stderr, "RW021_RDP_FILL_RECT w0=0x%08X w1=0x%08X coords=(%.2f,%.2f)-(%.2f,%.2f) fill_rgba=(%u,%u,%u,%u)\n", w0, w1, left, top, right, bottom, static_cast<unsigned>(fill_color_.r), static_cast<unsigned>(fill_color_.g), static_cast<unsigned>(fill_color_.b), static_cast<unsigned>(fill_color_.a));
                XR64_RENDER_DIAGNOSTIC_FLUSH();
#endif
                N64RawFast3DDrawState state = draw_state();
                state.textured = false;
                state.fill_color = fill_color_;
                const bool submitted = backend_.draw_rectangle(left, top, right, bottom, 0, 0, 0, 0, state, error_);
#if XR64_RENDER_DIAGNOSTICS
                if (submitted && stats_ != nullptr) ++stats_->rectangle_count;
#endif
                return submitted;
            }
            case kLoadSync: case kPipeSync: case kTileSync: case kFullSync: case kPrimDepth: case kSetConvert: case kSetKeyR: case kSetKeyGb: return true;
            default: { std::ostringstream stream; stream << "raw_fast3d_unsupported_opcode_0x" << std::hex << static_cast<unsigned>(opcode); error_ = stream.str(); return false; }
        }
    }
};

} // namespace

bool execute_n64_raw_fast3d_task(
        const N64GraphicsTaskDescriptor &task,
        const std::uint8_t *rdram,
        std::size_t rdram_size,
        N64RawFast3DBackend &backend,
        std::string &error,
        N64RawFast3DTaskStats *stats, N64RawFast3DListReplacement *replacement) {
    if (stats != nullptr) *stats = {};
    Executor executor(task, rdram, rdram_size, backend, error, stats, replacement);
    return executor.run();
}

} // namespace xr64
