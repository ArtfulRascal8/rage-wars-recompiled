#pragma once

#include "xr64_fast3d/n64_raw_fast3d_renderer.hpp"
#include <Windows.h>
#include <GL/gl.h>
#include <cstdint>
#include <string>

namespace xr64::rage_wars::recomp {

// Qualified by the October 8 captured lit CI4/CI8 model draws. Other materials,
// point/average sampling, texture generation and XR retain their current paths.
inline bool captured_model_filter_material(const N64RawFast3DDrawState& state,
        unsigned format, unsigned size) {
    return state.textured && format == 2U && size <= 1U &&
            (state.geometry_mode & 0x00020000U) != 0U &&
            (state.geometry_mode & 0x000C0000U) == 0U &&
            ((state.other_mode >> 52U) & 3U) == 1U &&
            ((state.other_mode >> 44U) & 3U) == 2U &&
            state.combine_word0 == 0xFC1217FFU &&
            state.combine_word1 == 0xFFFFFE38U;
}

// Fragment-only compatibility program: keep the established fixed-function
// vertex, clipping, color, blend/depth and perspective-q submission paths.
// This approximates the RDP filter, not its complete fixed-point rasterizer.
class CapturedModelFilter {
public:
    static constexpr const char* fragment_source = R"GLSL(#version 120
uniform sampler2D u_texture;
uniform vec2 u_size;
void main() {
    vec2 st = gl_TexCoord[0].st / gl_TexCoord[0].q * u_size;
    vec2 base = floor(st);
    // RDP filtered integer ST addresses texel centers. Do not subtract the
    // OpenGL half texel here. Fetch explicit centers through GL_NEAREST.
    vec2 f = floor(fract(st) * 32.0) / 32.0;
    vec2 center = (base + vec2(0.5)) / u_size;
    vec4 texel;
    if (f.x + f.y < 1.0) {
        vec4 a = texture2D(u_texture, center);
        texel = a + f.x * (texture2D(u_texture,
                center + vec2(1.0, 0.0) / u_size) - a)
                  + f.y * (texture2D(u_texture,
                center + vec2(0.0, 1.0) / u_size) - a);
    } else {
        vec4 a = texture2D(u_texture, center + vec2(1.0) / u_size);
        texel = a + (1.0 - f.x) * (texture2D(u_texture,
                center + vec2(0.0, 1.0) / u_size) - a)
                  + (1.0 - f.y) * (texture2D(u_texture,
                center + vec2(1.0, 0.0) / u_size) - a);
    }
    gl_FragColor = texel * gl_Color;
}
)GLSL";

    bool begin(unsigned width, unsigned height) {
        if (bound_ || width == 0 || height == 0 || !ensure()) return false;
        GLint incoming = 0;
        glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &incoming);
        incoming_program_ = static_cast<GLuint>(incoming);
        use_program_(program_);
        if (width != width_ || height != height_) {
            uniform_2f_(size_, static_cast<GLfloat>(width), static_cast<GLfloat>(height));
            width_ = width; height_ = height;
        }
        bound_ = true;
        return true;
    }
    void end() {
        if (bound_) { use_program_(incoming_program_); bound_ = false; }
    }
    const std::string& reason() const { return reason_; }
    void release_current_context() {
        if (owner_ && owner_ == wglGetCurrentContext()) {
            end();
            if (program_ && delete_program_) delete_program_(program_);
        }
        *this = CapturedModelFilter{};
    }
private:
    using CreateShader = GLuint (APIENTRY *)(GLenum);
    using ShaderSource = void (APIENTRY *)(GLuint, GLsizei, const char* const*, const GLint*);
    using CompileShader = void (APIENTRY *)(GLuint);
    using GetShader = void (APIENTRY *)(GLuint, GLenum, GLint*);
    using GetShaderLog = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, char*);
    using DeleteShader = void (APIENTRY *)(GLuint);
    using CreateProgram = GLuint (APIENTRY *)();
    using AttachShader = void (APIENTRY *)(GLuint, GLuint);
    using LinkProgram = void (APIENTRY *)(GLuint);
    using GetProgram = void (APIENTRY *)(GLuint, GLenum, GLint*);
    using GetProgramLog = void (APIENTRY *)(GLuint, GLsizei, GLsizei*, char*);
    using DeleteProgram = void (APIENTRY *)(GLuint);
    using UseProgram = void (APIENTRY *)(GLuint);
    using GetUniform = GLint (APIENTRY *)(GLuint, const char*);
    using Uniform1i = void (APIENTRY *)(GLint, GLint);
    using Uniform2f = void (APIENTRY *)(GLint, GLfloat, GLfloat);
    DeleteProgram delete_program_ = nullptr;
    UseProgram use_program_ = nullptr;
    Uniform2f uniform_2f_ = nullptr;
    HGLRC owner_ = nullptr;
    GLuint program_ = 0, incoming_program_ = 0;
    GLint size_ = -1;
    unsigned width_ = 0, height_ = 0;
    bool initialized_ = false, bound_ = false;
    std::string reason_ = "not_initialized";
    template<class T> static T proc(const char* name) {
        const auto p = wglGetProcAddress(name);
        const auto v = reinterpret_cast<std::intptr_t>(p);
        return v == 0 || v == 1 || v == 2 || v == 3 || v == -1 ?
                nullptr : reinterpret_cast<T>(p);
    }
    bool ensure() {
        const HGLRC context = wglGetCurrentContext();
        if (!context) { reason_ = "no_current_context"; return false; }
        if (initialized_) {
            if (owner_ != context) { reason_ = "context_changed_without_shutdown"; return false; }
            return program_ != 0;
        }
        initialized_ = true; owner_ = context;
#define XR64_FILTER_PROC(type, local, name) \
        const auto local = proc<type>(name); \
        if (!local) { reason_ = std::string("entry_point_unavailable:") + name; return false; }
        XR64_FILTER_PROC(CreateShader, create_shader, "glCreateShader")
        XR64_FILTER_PROC(ShaderSource, shader_source, "glShaderSource")
        XR64_FILTER_PROC(CompileShader, compile_shader, "glCompileShader")
        XR64_FILTER_PROC(GetShader, get_shader, "glGetShaderiv")
        XR64_FILTER_PROC(GetShaderLog, shader_log, "glGetShaderInfoLog")
        XR64_FILTER_PROC(DeleteShader, delete_shader, "glDeleteShader")
        XR64_FILTER_PROC(CreateProgram, create_program, "glCreateProgram")
        XR64_FILTER_PROC(AttachShader, attach_shader, "glAttachShader")
        XR64_FILTER_PROC(LinkProgram, link_program, "glLinkProgram")
        XR64_FILTER_PROC(GetProgram, get_program, "glGetProgramiv")
        XR64_FILTER_PROC(GetProgramLog, program_log, "glGetProgramInfoLog")
        XR64_FILTER_PROC(DeleteProgram, delete_program, "glDeleteProgram")
        XR64_FILTER_PROC(UseProgram, use_program, "glUseProgram")
        XR64_FILTER_PROC(GetUniform, get_uniform, "glGetUniformLocation")
        XR64_FILTER_PROC(Uniform1i, uniform_1i, "glUniform1i")
        XR64_FILTER_PROC(Uniform2f, uniform_2f, "glUniform2f")
#undef XR64_FILTER_PROC
        delete_program_ = delete_program; use_program_ = use_program; uniform_2f_ = uniform_2f;
        const GLuint shader = create_shader(0x8B30 /* GL_FRAGMENT_SHADER */);
        if (!shader) { reason_ = "shader_allocation_failed"; return false; }
        shader_source(shader, 1, &fragment_source, nullptr);
        compile_shader(shader);
        GLint ok = 0; get_shader(shader, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
        char log[1024]{};
        if (!ok) {
            shader_log(shader, sizeof(log), nullptr, log); delete_shader(shader);
            reason_ = std::string("shader_compile_failed:") + log; return false;
        }
        program_ = create_program();
        if (!program_) { delete_shader(shader); reason_ = "program_allocation_failed"; return false; }
        attach_shader(program_, shader); link_program(program_); delete_shader(shader);
        get_program(program_, 0x8B82 /* GL_LINK_STATUS */, &ok);
        size_ = get_uniform(program_, "u_size");
        const GLint sampler = get_uniform(program_, "u_texture");
        if (!ok || size_ < 0 || sampler < 0) {
            program_log(program_, sizeof(log), nullptr, log); delete_program(program_); program_ = 0;
            reason_ = std::string("program_link_or_interface_failed:") + log; return false;
        }
        GLint incoming = 0; glGetIntegerv(0x8B8D, &incoming);
        use_program(program_); uniform_1i(sampler, 0); use_program(static_cast<GLuint>(incoming));
        reason_ = "ready";
        return true;
    }
};

struct CapturedModelFilterScope {
    CapturedModelFilter* filter = nullptr;
    ~CapturedModelFilterScope() { if (filter) filter->end(); }
    CapturedModelFilterScope() = default;
    CapturedModelFilterScope(const CapturedModelFilterScope&) = delete;
    CapturedModelFilterScope& operator=(const CapturedModelFilterScope&) = delete;
};

} // namespace xr64::rage_wars::recomp
