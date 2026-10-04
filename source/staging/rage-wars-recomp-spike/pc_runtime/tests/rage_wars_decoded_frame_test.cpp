#include "xr64_fast3d/n64_decoded_frame.hpp"
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include "xr64_fast3d/n64_frame_mailbox.hpp"

using namespace xr64;

struct Witness : N64RawFast3DBackend {
    int calls = 0;
    bool valid = true;
    int fail = 0;
    std::uint64_t expected_texture_hash = 0;
    std::vector<int> order;
    std::vector<const N64RawFast3DVertex*> triangle_data;

    bool record(int command, bool arguments_valid, std::string& error) {
        ++calls;
        order.push_back(command);
        valid = valid && arguments_valid;
        if (calls == fail) {
            error = "injected";
            return false;
        }
        return true;
    }

    bool begin_frame(std::uint32_t width, std::uint32_t height,
            std::string& error) override {
        return record(1, width == 320 && height == 240, error);
    }

    bool set_viewport(int x, int y, int width, int height,
            std::string& error) override {
        return record(2, x == -3 && y == 4 && width == 320 && height == 240,
                error);
    }

    bool set_scissor(int x, int y, int width, int height,
            std::string& error) override {
        return record(3, x == 1 && y == 2 && width == 300 && height == 200,
                error);
    }

    bool set_color_image(std::uint32_t address, std::uint32_t width,
            std::string& error) override {
        return record(4, address == 0x123456U && width == 640, error);
    }

    bool set_depth_image(std::uint32_t address, std::string& error) override {
        return record(5, address == 0x654321U, error);
    }

    bool upload_texture(const N64RawFast3DTexture& texture,
            std::string& error) override {
        const bool matches = texture.width == 1 && texture.height == 1 &&
                texture.rgba == std::vector<std::uint8_t>{71, 2, 3, 4} &&
                texture.content_hash_valid &&
                texture.content_hash == expected_texture_hash;
        return record(6, matches, error);
    }

    bool draw_triangles(const std::vector<N64RawFast3DVertex>&,
            const N64RawFast3DDrawState&, std::string& error) override {
        return record(7, false, error);
    }

    bool draw_triangles_view(const N64RawFast3DVertex* vertices,
            std::size_t count, const N64RawFast3DDrawState& state,
            std::string& error) override {
        const auto ordinal = triangle_data.size();
        triangle_data.push_back(vertices);
        if (ordinal == 0) {
            const bool matches = vertices != nullptr && count == 3 &&
                    vertices[0].x == 0.25F &&
                    state.geometry_mode == 123 && state.alpha_blend;
            return record(7, matches, error);
        }
        if (ordinal == 1) {
            const bool matches = vertices != nullptr && count == 2 &&
                    vertices[0].x == 0.75F &&
                    state.geometry_mode == 456 && !state.alpha_blend;
            return record(8, matches, error);
        }
        return record(99, false, error);
    }

    bool draw_rectangle(float left, float top, float right, float bottom,
            float s0, float t0, float s1, float t1,
            const N64RawFast3DDrawState& state, std::string& error) override {
        const bool matches = left == 10.0F && top == 20.0F &&
                right == 30.0F && bottom == 40.0F &&
                s0 == 0.0F && t0 == 0.25F && s1 == 1.0F && t1 == 0.75F &&
                state.geometry_mode == 123 && state.alpha_blend;
        return record(9, matches, error);
    }

    bool clear_depth(std::string& error) override {
        return record(10, true, error);
    }

    bool end_frame(std::string& error) override {
        return record(11, true, error);
    }
};

int main() {
    N64DecodedFrame frame;
    std::string error;
    std::uint64_t expected_texture_hash = 0;
    {
        N64RawFast3DTexture texture;
        texture.rgba = {71, 2, 3, 4};
        expected_texture_hash = n64_raw_fast3d_texture_hash(texture);
        std::vector<N64RawFast3DVertex> vertices(3);
        vertices[0].x = 0.25F;
        std::vector<N64RawFast3DVertex> second_vertices(2);
        second_vertices[0].x = 0.75F;
        N64RawFast3DDrawState state;
        state.geometry_mode = 123;
        state.alpha_blend = true;
        N64RawFast3DDrawState second_state;
        second_state.geometry_mode = 456;

        if (!frame.begin_frame(320, 240, error) ||
                !frame.set_viewport(-3, 4, 320, 240, error) ||
                !frame.set_scissor(1, 2, 300, 200, error) ||
                !frame.set_color_image(0x123456U, 640, error) ||
                !frame.set_depth_image(0x654321U, error) ||
                !frame.upload_texture(texture, error))
            return 1;
        if (texture.content_hash_valid) return 1;
        texture.rgba[0] = 99;
        if (!frame.draw_triangles(vertices, state, error) ||
                !frame.draw_triangles(second_vertices, second_state, error) ||
                !frame.draw_rectangle(10.0F, 20.0F, 30.0F, 40.0F,
                        0.0F, 0.25F, 1.0F, 0.75F, state, error) ||
                !frame.clear_depth(error) ||
                !frame.end_frame(error))
            return 1;
        vertices[0].x = 99.0F;
        second_vertices[0].x = 99.0F;
        state.geometry_mode = 999;
        second_state.geometry_mode = 999;
    }

    const std::vector<int> expected_order{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    if (frame.size() != expected_order.size()) return 2;
    std::size_t span_offset = 99;
    if (frame.triangle_vertex_count() != 5 ||
            !frame.triangle_span_offset(frame.triangle_vertex_data(), 3,
                    span_offset) || span_offset != 0 ||
            !frame.triangle_span_offset(frame.triangle_vertex_data() + 3, 2,
                    span_offset) || span_offset != 3 ||
            frame.triangle_span_offset(frame.triangle_vertex_data() + 4, 2,
                    span_offset) ||
            frame.triangle_span_offset(nullptr, 3, span_offset))
        return 10;

    Witness left;
    Witness right;
    left.expected_texture_hash = expected_texture_hash;
    right.expected_texture_hash = expected_texture_hash;
    if (!frame.replay(left, error) || !frame.replay(right, error) ||
            !left.valid || !right.valid || left.calls != 11 || right.calls != 11 ||
            left.order != expected_order || right.order != expected_order ||
            left.triangle_data.size() != 2 || right.triangle_data.size() != 2 ||
            left.triangle_data != right.triangle_data ||
            reinterpret_cast<std::uintptr_t>(left.triangle_data[0]) +
                    3 * sizeof(N64RawFast3DVertex) !=
                    reinterpret_cast<std::uintptr_t>(left.triangle_data[1]))
        return 3;

    Witness broken;
    broken.expected_texture_hash = expected_texture_hash;
    broken.fail = 8;
    if (frame.replay(broken, error) || broken.calls != 8 ||
            broken.order != std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8} ||
            error != "injected")
        return 4;

    N64FrameMailbox mailbox;
    if (mailbox.acquire()) return 5;
    mailbox.publish({std::move(frame), 1, 0.25});
    const auto held = mailbox.acquire();
    mailbox.publish({N64DecodedFrame{}, 2, 0.5});
    if (held->generation != 1 || mailbox.acquire()->generation != 2 ||
            !held->frame.triangle_span_offset(left.triangle_data[1], 2,
                    span_offset) || span_offset != 3)
        return 6;

    for (int i = 0; i < 120; ++i) {
        Witness repeated;
        repeated.expected_texture_hash = expected_texture_hash;
        if (!held->frame.replay(repeated, error) || !repeated.valid ||
                repeated.calls != 11 || repeated.order != expected_order ||
                repeated.triangle_data != left.triangle_data)
            return 7;
    }

    mailbox.reset();
    if (mailbox.acquire() || held->frame.size() != 11) return 8;

    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (unsigned i = 3; i < 1003; ++i)
            mailbox.publish({N64DecodedFrame{}, i, 0});
        done = true;
    });
    bool valid = true;
    do {
        const auto current = mailbox.acquire();
        if (current && current->generation < 3) valid = false;
    } while (!done.load());
    producer.join();
    if (!valid || mailbox.acquire()->generation != 1002 ||
            held->generation != 1)
        return 9;

    return 0;
}
