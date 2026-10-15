#pragma once

#include "n64_raw_fast3d_renderer.hpp"

#include <cstdint>
#include <limits>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xr64 {

// Own all decoded payloads: replay never reads guest memory or decoder scratch.
// The compact command list stores only an opcode and an index into a typed
// payload array. Replay therefore preserves the recorded order without one
// heap-allocated type-erased callable per command.
class N64DecodedFrame final : public N64RawFast3DBackend {
    enum class Op : std::uint8_t {
        BeginFrame,
        SetViewport,
        SetScissor,
        SetColorImage,
        SetDepthImage,
        UploadTexture,
        DrawTriangles,
        DrawRectangle,
        ClearDepth,
        EndFrame
    };

    struct Command {
        Op op;
        std::uint32_t payload_index;
    };

    struct FrameSize {
        std::uint32_t width;
        std::uint32_t height;
    };

    struct Rect {
        int x;
        int y;
        int width;
        int height;
    };

    struct ColorImage {
        std::uint32_t address;
        std::uint32_t width;
    };

    struct TriangleBatch {
        std::size_t vertex_offset;
        std::size_t vertex_count;
        N64RawFast3DDrawState state;
    };

    struct RectangleDraw {
        float left;
        float top;
        float right;
        float bottom;
        float s0;
        float t0;
        float s1;
        float t1;
        N64RawFast3DDrawState state;
    };

    std::vector<Command> commands_;
    std::vector<FrameSize> frame_sizes_;
    std::vector<Rect> rects_;
    std::vector<ColorImage> color_images_;
    std::vector<std::uint32_t> depth_images_;
    std::vector<N64RawFast3DTexture> textures_;
    std::vector<N64RawFast3DVertex> triangle_vertices_;
    std::vector<TriangleBatch> triangle_batches_;
    std::vector<RectangleDraw> rectangle_draws_;
    // The game reloads the same texture for many draws in a frame. Identical
    // loads share one owned payload instead of each copying its texels.
    std::unordered_map<std::uint64_t, std::uint32_t> texture_by_hash_;

    template <typename Payload>
    bool append_payload(Op op, std::vector<Payload>& payloads,
            Payload payload, std::string& error) {
        if (payloads.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            error = "decoded frame payload index exceeds 32-bit command stream limit";
            return false;
        }

        const auto index = static_cast<std::uint32_t>(payloads.size());
        payloads.emplace_back(std::move(payload));
        try {
            commands_.push_back({op, index});
        } catch (...) {
            payloads.pop_back();
            throw;
        }
        return true;
    }

    static bool same_texture(const N64RawFast3DTexture& a,
            const N64RawFast3DTexture& b) {
        return a.alpha_precombined == b.alpha_precombined &&
                a.wrap_s == b.wrap_s && a.wrap_t == b.wrap_t &&
                a.width == b.width && a.height == b.height &&
                a.format == b.format && a.size == b.size &&
                a.rgba.size() == b.rgba.size() &&
                (a.rgba.empty() ||
                    std::memcmp(a.rgba.data(), b.rgba.data(), a.rgba.size()) == 0);
    }

    bool append_marker(Op op) {
        commands_.push_back({op, 0});
        return true;
    }

    bool append_triangles(const std::vector<N64RawFast3DVertex>& vertices,
            const N64RawFast3DDrawState& state, std::string& error) {
        if (triangle_batches_.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            error = "decoded frame triangle index exceeds 32-bit command stream limit";
            return false;
        }

        const auto vertex_offset = triangle_vertices_.size();
        if (vertices.size() > triangle_vertices_.max_size() - vertex_offset) {
            error = "decoded frame vertex arena exceeds vector capacity";
            return false;
        }

        const auto batch_index =
                static_cast<std::uint32_t>(triangle_batches_.size());
        triangle_vertices_.insert(triangle_vertices_.end(),
                vertices.begin(), vertices.end());
        try {
            triangle_batches_.push_back(
                    {vertex_offset, vertices.size(), state});
            try {
                commands_.push_back({Op::DrawTriangles, batch_index});
            } catch (...) {
                triangle_batches_.pop_back();
                throw;
            }
        } catch (...) {
            triangle_vertices_.resize(vertex_offset);
            throw;
        }
        return true;
    }

public:
    bool replay(N64RawFast3DBackend& backend, std::string& error) const {
        for (const auto& command : commands_) {
            const auto index = static_cast<std::size_t>(command.payload_index);
            switch (command.op) {
            case Op::BeginFrame: {
                const auto& payload = frame_sizes_[index];
                if (!backend.begin_frame(payload.width, payload.height, error))
                    return false;
                break;
            }
            case Op::SetViewport: {
                const auto& payload = rects_[index];
                if (!backend.set_viewport(payload.x, payload.y,
                        payload.width, payload.height, error))
                    return false;
                break;
            }
            case Op::SetScissor: {
                const auto& payload = rects_[index];
                if (!backend.set_scissor(payload.x, payload.y,
                        payload.width, payload.height, error))
                    return false;
                break;
            }
            case Op::SetColorImage: {
                const auto& payload = color_images_[index];
                if (!backend.set_color_image(payload.address, payload.width, error))
                    return false;
                break;
            }
            case Op::SetDepthImage:
                if (!backend.set_depth_image(depth_images_[index], error))
                    return false;
                break;
            case Op::UploadTexture:
                if (!backend.upload_texture(textures_[index], error))
                    return false;
                break;
            case Op::DrawTriangles: {
                const auto& payload = triangle_batches_[index];
                const auto* vertex_data = payload.vertex_count == 0
                        ? nullptr
                        : triangle_vertices_.data() + payload.vertex_offset;
                if (!backend.draw_triangles_view(vertex_data,
                        payload.vertex_count, payload.state, error))
                    return false;
                break;
            }
            case Op::DrawRectangle: {
                const auto& payload = rectangle_draws_[index];
                if (!backend.draw_rectangle(payload.left, payload.top,
                        payload.right, payload.bottom, payload.s0, payload.t0,
                        payload.s1, payload.t1, payload.state, error))
                    return false;
                break;
            }
            case Op::ClearDepth:
                if (!backend.clear_depth(error)) return false;
                break;
            case Op::EndFrame:
                if (!backend.end_frame(error)) return false;
                break;
            }
        }
        return true;
    }

    std::size_t size() const { return commands_.size(); }

    // Read-only batch listing in replay order, with the content hash of the
    // texture most recently uploaded before each batch (0 when none).
    struct TriangleBatchView {
        const N64RawFast3DVertex* vertices;
        std::size_t vertex_count;
        const N64RawFast3DDrawState* state;
        std::uint64_t texture_hash;
    };
    std::vector<TriangleBatchView> triangle_batch_views() const {
        std::vector<TriangleBatchView> views;
        views.reserve(triangle_batches_.size());
        std::uint64_t texture = 0;
        for (const auto& command : commands_) {
            if (command.op == Op::UploadTexture) {
                texture = textures_[command.payload_index].content_hash;
            } else if (command.op == Op::DrawTriangles) {
                const auto& batch = triangle_batches_[command.payload_index];
                views.push_back({batch.vertex_count ? triangle_vertices_.data() + batch.vertex_offset : nullptr,
                        batch.vertex_count, &batch.state, texture});
            }
        }
        return views;
    }

    // The graphics owner may retain this immutable arena for one generation.
    // Accept a draw pointer only when it names a complete span in the arena.
    const N64RawFast3DVertex* triangle_vertex_data() const noexcept {
        return triangle_vertices_.data();
    }
    std::size_t triangle_vertex_count() const noexcept {
        return triangle_vertices_.size();
    }
    bool triangle_span_offset(const N64RawFast3DVertex* data,
            std::size_t count, std::size_t& offset) const noexcept {
        if (!data || !count || triangle_vertices_.empty()) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(triangle_vertices_.data());
        const auto address = reinterpret_cast<std::uintptr_t>(data);
        if (address < base) return false;
        const auto byte_offset = address - base;
        if (byte_offset % sizeof(N64RawFast3DVertex) != 0) return false;
        const auto candidate = byte_offset / sizeof(N64RawFast3DVertex);
        if (candidate > triangle_vertices_.size() ||
                count > triangle_vertices_.size() - candidate) return false;
        offset = candidate;
        return true;
    }

    bool begin_frame(std::uint32_t width, std::uint32_t height,
            std::string& error) override {
        return append_payload(Op::BeginFrame, frame_sizes_,
                FrameSize{width, height}, error);
    }

    bool set_viewport(int x, int y, int width, int height,
            std::string& error) override {
        return append_payload(Op::SetViewport, rects_,
                Rect{x, y, width, height}, error);
    }

    bool set_scissor(int x, int y, int width, int height,
            std::string& error) override {
        return append_payload(Op::SetScissor, rects_,
                Rect{x, y, width, height}, error);
    }

    bool set_color_image(std::uint32_t address, std::uint32_t width,
            std::string& error) override {
        return append_payload(Op::SetColorImage, color_images_,
                ColorImage{address, width}, error);
    }

    bool set_depth_image(std::uint32_t address, std::string& error) override {
        return append_payload(Op::SetDepthImage, depth_images_, address, error);
    }

    bool upload_texture(const N64RawFast3DTexture& texture,
            std::string& error) override {
        const std::uint64_t hash = n64_raw_fast3d_texture_hash(texture);
        const auto found = texture_by_hash_.find(hash);
        if (found != texture_by_hash_.end() &&
                same_texture(textures_[found->second], texture)) {
            commands_.push_back({Op::UploadTexture, found->second});
            return true;
        }
        N64RawFast3DTexture owned_texture = texture;
        owned_texture.content_hash = hash;
        owned_texture.content_hash_valid = true;
        const auto index = static_cast<std::uint32_t>(textures_.size());
        if (!append_payload(Op::UploadTexture, textures_,
                std::move(owned_texture), error)) return false;
        texture_by_hash_[hash] = index;
        return true;
    }

    bool draw_triangles(const std::vector<N64RawFast3DVertex>& vertices,
            const N64RawFast3DDrawState& state, std::string& error) override {
        return append_triangles(vertices, state, error);
    }

    bool draw_rectangle(float left, float top, float right, float bottom,
            float s0, float t0, float s1, float t1,
            const N64RawFast3DDrawState& state, std::string& error) override {
        return append_payload(Op::DrawRectangle, rectangle_draws_,
                RectangleDraw{left, top, right, bottom, s0, t0, s1, t1, state},
                error);
    }

    bool clear_depth(std::string&) override {
        return append_marker(Op::ClearDepth);
    }

    bool end_frame(std::string&) override {
        return append_marker(Op::EndFrame);
    }
};

} // namespace xr64
