#pragma once

#include "xr64_fast3d/n64_raw_fast3d_renderer.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <GL/gl.h>
#endif
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <tuple>

#include "rage_wars_feature_defaults.hpp"

namespace xr64::rage_wars::recomp {

// RDP colour combiner (G_SETCOMBINE). Each cycle computes (A - B) * C + D for
// RGB and alpha from per-draw selectors. Decoded from the two command words.
struct CombinerCycle { int a, b, c, d, aa, ab, ac, ad; };
struct CombinerSelectors { CombinerCycle cycle[2]; };

inline CombinerSelectors decode_combiner(std::uint32_t w0, std::uint32_t w1) {
    CombinerSelectors s{};
    s.cycle[0] = {int((w0 >> 20) & 0xF), int((w1 >> 28) & 0xF), int((w0 >> 15) & 0x1F), int((w1 >> 15) & 0x7),
                  int((w0 >> 12) & 0x7), int((w1 >> 12) & 0x7), int((w0 >> 9) & 0x7), int((w1 >> 9) & 0x7)};
    s.cycle[1] = {int((w0 >> 5) & 0xF), int((w1 >> 24) & 0xF), int(w0 & 0x1F), int((w1 >> 6) & 0x7),
                  int((w1 >> 21) & 0x7), int((w1 >> 3) & 0x7), int((w1 >> 18) & 0x7), int(w1 & 0x7)};
    return s;
}

// On by default (XR64_GENERIC_COMBINER=0 switches it off). Two existing special materials and the
// captured-model filter keep their paths; XR eyes are excluded by the caller.
inline bool generic_combiner_enabled() {
    static const bool enabled = [] {
        return feature_default_on("XR64_GENERIC_COMBINER");
    }();
    return enabled;
}
inline bool combiner_log_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("XR64_COMBINER_LOG");
        return v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}

// Two-cycle mode chains cycle 0 into cycle 1. One-cycle mode uses the second
// selector set (the RDP's cycle-1 inputs); games normally program both alike.
inline bool combiner_two_cycle(const N64RawFast3DDrawState& state) {
    return ((state.other_mode >> 52U) & 3U) == 1U;
}

// Two-colour text (HUD fonts, kill log): RGB = lerp(from, to, TEXEL) with the two
// colours coming from the primitive and environment registers, i.e. (A - B) * T + D
// with {A, B, D} = {ENV, PRIM, PRIM} or {PRIM, ENV, ENV}, alpha = TEXEL_A times one
// register. Texture rectangles use the primitive colour only by default, so this
// text drew in the primitive colour (black for the kill log). OpenGL's GL_BLEND
// texture environment computes exactly Cf * (1 - Ct) + Cc * Ct, so the rectangle
// path can draw it with Cf = the T=0 colour and Cc = the T=1 colour.
struct RectLerp {
    bool valid = false;
    bool from_prim = false; // T=0 colour is the primitive colour (else environment)
    int alpha_source = 0;   // 0 = texel alpha only, 1 = x primitive alpha, 2 = x environment alpha
};
inline RectLerp combiner_rect_lerp(const CombinerSelectors& s, bool two_cycle) {
    RectLerp r;
    const auto& c = s.cycle[two_cycle ? 0 : 1];
    if (!(c.c == 1 || c.c == 2 || c.c == 8 || c.c == 9)) return r; // TEXEL0/1 or their alpha
    if (c.a == 5 && c.b == 3 && c.d == 3) r.from_prim = true;       // lerp(PRIM, ENV, T)
    else if (c.a == 3 && c.b == 5 && c.d == 5) r.from_prim = false; // lerp(ENV, PRIM, T)
    else return r;
    // Alpha must be TEXEL_A * (PRIM_A | ENV_A | 1) with nothing else added.
    if (!((c.aa == 1 || c.aa == 2) && c.ab == 7 && c.ad == 7)) return r;
    if (c.ac != 3 && c.ac != 5 && c.ac != 7) return r;
    // In two-cycle mode the second cycle must pass the first cycle's result through.
    if (two_cycle) {
        const auto& d = s.cycle[1];
        if (!(d.d == 0 && d.a >= 8 && d.c >= 16 && d.ad == 0 && d.aa == 7 && d.ac == 7)) return r;
    }
    r.valid = true;
    r.alpha_source = c.ac == 3 ? 1 : c.ac == 5 ? 2 : 0;
    return r;
}
inline bool rect_combiner_color_enabled() {
    static const bool enabled = [] {
        return feature_default_on("XR64_RECT_COMBINER_COLOR");
    }();
    return enabled;
}

// CPU reference for tests: inputs are RGBA in [0,1]. TEXEL1 aliases TEXEL0
// (the decoded payload carries one texture); NOISE, CENTER/SCALE/K4/K5 and the
// LOD fractions use neutral values documented in the shader.
struct CombinerInputs { std::array<float, 4> texel, shade, prim, env; float prim_lod = 0; };
inline std::array<float, 4> combiner_reference(const CombinerSelectors& s, bool two_cycle, const CombinerInputs& in) {
    std::array<float, 4> combined{0, 0, 0, 0};
    const auto rgb = [&](int sel, int kind) -> std::array<float, 3> {
        // kind: 0=A, 1=B, 2=C, 3=D
        const std::array<float, 3> zero{0, 0, 0}, one{1, 1, 1};
        auto c3 = [](const std::array<float, 4>& v) { return std::array<float, 3>{v[0], v[1], v[2]}; };
        auto a3 = [](float a) { return std::array<float, 3>{a, a, a}; };
        if (kind == 3 && sel >= 7) return zero;
        switch (sel) {
        case 0: return c3(combined);
        case 1: case 2: return c3(in.texel);
        case 3: return c3(in.prim);
        case 4: return c3(in.shade);
        case 5: return c3(in.env);
        case 6: return kind == 0 || kind == 3 ? one : zero; // ONE for A/D; CENTER/SCALE neutral
        }
        if (kind == 2) {
            switch (sel) {
            case 7: return a3(combined[3]);
            case 8: case 9: return a3(in.texel[3]);
            case 10: return a3(in.prim[3]);
            case 11: return a3(in.shade[3]);
            case 12: return a3(in.env[3]);
            case 14: return a3(in.prim_lod);
            }
        }
        return zero;
    };
    const auto alpha = [&](int sel, bool is_c) -> float {
        switch (sel) {
        case 0: return is_c ? 0.0F : combined[3];
        case 1: case 2: return in.texel[3];
        case 3: return in.prim[3];
        case 4: return in.shade[3];
        case 5: return in.env[3];
        case 6: return is_c ? in.prim_lod : 1.0F;
        default: return 0.0F;
        }
    };
    for (int i = two_cycle ? 0 : 1; i < 2; ++i) {
        const auto& c = s.cycle[i];
        const auto A = rgb(c.a, 0), B = rgb(c.b, 1), C = rgb(c.c, 2), D = rgb(c.d, 3);
        std::array<float, 4> out{};
        for (int k = 0; k < 3; ++k) out[k] = (A[k] - B[k]) * C[k] + D[k];
        out[3] = (alpha(c.aa, false) - alpha(c.ab, false)) * alpha(c.ac, true) + alpha(c.ad, false);
        for (float& v : out) v = v < 0 ? 0 : v > 1 ? 1 : v;
        combined = out;
    }
    return combined;
}

#ifdef _WIN32
// Fragment program over the existing fixed-function vertex submission.
class GenericCombiner {
public:
    static constexpr const char* fragment_source = R"GLSL(#version 120
uniform sampler2D u_texture;
uniform int u_textured;
uniform int u_two_cycle;
uniform ivec4 u_rgb0, u_rgb1, u_alpha0, u_alpha1; // (a,b,c,d) per cycle
uniform vec4 u_prim, u_env;
uniform float u_prim_lod;
vec4 g_tex, g_shade, g_comb;
vec3 rgb_in(int s, int kind) {
    if (kind == 3 && s >= 7) return vec3(0.0);
    if (s == 0) return g_comb.rgb;
    if (s == 1 || s == 2) return g_tex.rgb;
    if (s == 3) return u_prim.rgb;
    if (s == 4) return g_shade.rgb;
    if (s == 5) return u_env.rgb;
    if (s == 6) return (kind == 0 || kind == 3) ? vec3(1.0) : vec3(0.0);
    if (kind == 2) {
        if (s == 7) return vec3(g_comb.a);
        if (s == 8 || s == 9) return vec3(g_tex.a);
        if (s == 10) return vec3(u_prim.a);
        if (s == 11) return vec3(g_shade.a);
        if (s == 12) return vec3(u_env.a);
        if (s == 14) return vec3(u_prim_lod);
    }
    return vec3(0.0);
}
float alpha_in(int s, bool is_c) {
    if (s == 0) return is_c ? 0.0 : g_comb.a;
    if (s == 1 || s == 2) return g_tex.a;
    if (s == 3) return u_prim.a;
    if (s == 4) return g_shade.a;
    if (s == 5) return u_env.a;
    if (s == 6) return is_c ? u_prim_lod : 1.0;
    return 0.0;
}
vec4 cycle(ivec4 r, ivec4 a) {
    vec3 rgb = (rgb_in(r.x, 0) - rgb_in(r.y, 1)) * rgb_in(r.z, 2) + rgb_in(r.w, 3);
    float al = (alpha_in(a.x, false) - alpha_in(a.y, false)) * alpha_in(a.z, true) + alpha_in(a.w, false);
    return clamp(vec4(rgb, al), 0.0, 1.0);
}
void main() {
    g_tex = u_textured != 0 ? texture2DProj(u_texture, gl_TexCoord[0]) : vec4(1.0);
    g_shade = gl_Color;
    g_comb = vec4(0.0);
    if (u_two_cycle != 0) g_comb = cycle(u_rgb0, u_alpha0);
    gl_FragColor = cycle(u_rgb1, u_alpha1);
}
)GLSL";

    // Binds the program and its per-draw uniforms; false keeps the old path.
    bool begin(const N64RawFast3DDrawState& state) {
        if (bound_ || !ensure()) return false;
        GLint incoming = 0;
        glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &incoming);
        incoming_program_ = static_cast<GLuint>(incoming);
        use_program_(program_);
        const auto s = decode_combiner(state.combine_word0, state.combine_word1);
        uniform_1i_(u_textured_, state.textured ? 1 : 0);
        uniform_1i_(u_two_cycle_, combiner_two_cycle(state) ? 1 : 0);
        uniform_4i_(u_rgb0_, s.cycle[0].a, s.cycle[0].b, s.cycle[0].c, s.cycle[0].d);
        uniform_4i_(u_rgb1_, s.cycle[1].a, s.cycle[1].b, s.cycle[1].c, s.cycle[1].d);
        uniform_4i_(u_alpha0_, s.cycle[0].aa, s.cycle[0].ab, s.cycle[0].ac, s.cycle[0].ad);
        uniform_4i_(u_alpha1_, s.cycle[1].aa, s.cycle[1].ab, s.cycle[1].ac, s.cycle[1].ad);
        const auto& p = state.primitive_color;
        const auto& e = state.environment_color;
        uniform_4f_(u_prim_, p.r / 255.0F, p.g / 255.0F, p.b / 255.0F, p.a / 255.0F);
        uniform_4f_(u_env_, e.r / 255.0F, e.g / 255.0F, e.b / 255.0F, e.a / 255.0F);
        uniform_1f_(u_prim_lod_, 0.0F);
        bound_ = true;
        return true;
    }
    void end() { if (bound_) { use_program_(incoming_program_); bound_ = false; } }
    const std::string& reason() const { return reason_; }

private:
    template<class T> static T proc(const char* name) {
        const auto p = wglGetProcAddress(name);
        const auto v = reinterpret_cast<std::intptr_t>(p);
        return v == 0 || v == 1 || v == 2 || v == 3 || v == -1 ? nullptr : reinterpret_cast<T>(p);
    }
    using UseProgram = void (APIENTRY*)(GLuint);
    using Uniform1i = void (APIENTRY*)(GLint, GLint);
    using Uniform1f = void (APIENTRY*)(GLint, GLfloat);
    using Uniform4i = void (APIENTRY*)(GLint, GLint, GLint, GLint, GLint);
    using Uniform4f = void (APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    UseProgram use_program_ = nullptr;
    Uniform1i uniform_1i_ = nullptr;
    Uniform1f uniform_1f_ = nullptr;
    Uniform4i uniform_4i_ = nullptr;
    Uniform4f uniform_4f_ = nullptr;
    HGLRC owner_ = nullptr;
    GLuint program_ = 0, incoming_program_ = 0;
    GLint u_textured_ = -1, u_two_cycle_ = -1, u_rgb0_ = -1, u_rgb1_ = -1, u_alpha0_ = -1, u_alpha1_ = -1;
    GLint u_prim_ = -1, u_env_ = -1, u_prim_lod_ = -1;
    bool initialized_ = false, bound_ = false;
    std::string reason_ = "not_initialized";

    bool ensure() {
        const HGLRC context = wglGetCurrentContext();
        if (!context) { reason_ = "no_current_context"; return false; }
        if (initialized_) return owner_ == context && program_ != 0;
        initialized_ = true; owner_ = context;
        using CreateShader = GLuint (APIENTRY*)(GLenum);
        using ShaderSource = void (APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
        using CompileShader = void (APIENTRY*)(GLuint);
        using GetShader = void (APIENTRY*)(GLuint, GLenum, GLint*);
        using GetLog = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
        using DeleteShader = void (APIENTRY*)(GLuint);
        using CreateProgram = GLuint (APIENTRY*)();
        using AttachShader = void (APIENTRY*)(GLuint, GLuint);
        using LinkProgram = void (APIENTRY*)(GLuint);
        using GetUniform = GLint (APIENTRY*)(GLuint, const char*);
        const auto create_shader = proc<CreateShader>("glCreateShader");
        const auto shader_source = proc<ShaderSource>("glShaderSource");
        const auto compile_shader = proc<CompileShader>("glCompileShader");
        const auto get_shader = proc<GetShader>("glGetShaderiv");
        const auto shader_log = proc<GetLog>("glGetShaderInfoLog");
        const auto delete_shader = proc<DeleteShader>("glDeleteShader");
        const auto create_program = proc<CreateProgram>("glCreateProgram");
        const auto attach_shader = proc<AttachShader>("glAttachShader");
        const auto link_program = proc<LinkProgram>("glLinkProgram");
        const auto get_program = proc<GetShader>("glGetProgramiv");
        const auto program_log = proc<GetLog>("glGetProgramInfoLog");
        const auto get_uniform = proc<GetUniform>("glGetUniformLocation");
        use_program_ = proc<UseProgram>("glUseProgram");
        uniform_1i_ = proc<Uniform1i>("glUniform1i");
        uniform_1f_ = proc<Uniform1f>("glUniform1f");
        uniform_4i_ = proc<Uniform4i>("glUniform4i");
        uniform_4f_ = proc<Uniform4f>("glUniform4f");
        if (!create_shader || !shader_source || !compile_shader || !get_shader || !shader_log || !delete_shader ||
                !create_program || !attach_shader || !link_program || !get_program || !program_log || !get_uniform ||
                !use_program_ || !uniform_1i_ || !uniform_1f_ || !uniform_4i_ || !uniform_4f_) {
            reason_ = "entry_point_unavailable"; return false;
        }
        const GLuint shader = create_shader(0x8B30 /* GL_FRAGMENT_SHADER */);
        if (!shader) { reason_ = "shader_allocation_failed"; return false; }
        shader_source(shader, 1, &fragment_source, nullptr);
        compile_shader(shader);
        GLint ok = 0; get_shader(shader, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
        char log[1024]{};
        if (!ok) { shader_log(shader, sizeof(log), nullptr, log); delete_shader(shader);
            reason_ = std::string("shader_compile_failed:") + log; return false; }
        program_ = create_program();
        attach_shader(program_, shader); link_program(program_); delete_shader(shader);
        get_program(program_, 0x8B82 /* GL_LINK_STATUS */, &ok);
        if (!ok) { program_log(program_, sizeof(log), nullptr, log); program_ = 0;
            reason_ = std::string("program_link_failed:") + log; return false; }
        u_textured_ = get_uniform(program_, "u_textured"); u_two_cycle_ = get_uniform(program_, "u_two_cycle");
        u_rgb0_ = get_uniform(program_, "u_rgb0"); u_rgb1_ = get_uniform(program_, "u_rgb1");
        u_alpha0_ = get_uniform(program_, "u_alpha0"); u_alpha1_ = get_uniform(program_, "u_alpha1");
        u_prim_ = get_uniform(program_, "u_prim"); u_env_ = get_uniform(program_, "u_env");
        u_prim_lod_ = get_uniform(program_, "u_prim_lod");
        const GLint sampler = get_uniform(program_, "u_texture");
        GLint incoming = 0; glGetIntegerv(0x8B8D, &incoming);
        use_program_(program_); if (sampler >= 0) uniform_1i_(sampler, 0); use_program_(static_cast<GLuint>(incoming));
        reason_ = "ready";
        return true;
    }
};

struct GenericCombinerScope {
    GenericCombiner* combiner = nullptr;
    ~GenericCombinerScope() { if (combiner) combiner->end(); }
    GenericCombinerScope() = default;
    GenericCombinerScope(const GenericCombinerScope&) = delete;
    GenericCombinerScope& operator=(const GenericCombinerScope&) = delete;
};
#endif

// Opt-in inventory: each distinct material once, so combiners can be tied to
// on-screen objects (shadows, teleporter glow, reticles, Fireborn).
inline void log_combiner_once(const N64RawFast3DDrawState& state, unsigned format, unsigned size) {
    if (!combiner_log_enabled()) return;
    // The key includes the primitive and environment RGB (not alpha, which fades),
    // so a text draw that only differs by colour is logged too, with the time since
    // the first draw so it can be matched to an on-screen event.
    static std::set<std::tuple<std::uint32_t, std::uint32_t, unsigned, unsigned, bool, std::uint32_t,
                               std::uint32_t, std::uint32_t>> seen;
    static const auto start = std::chrono::steady_clock::now();
    const auto& p = state.primitive_color;
    const auto& e = state.environment_color;
    const auto key = std::make_tuple(state.combine_word0, state.combine_word1, format, size, state.textured,
                                     static_cast<std::uint32_t>((state.other_mode >> 52U) & 3U),
                                     (std::uint32_t(p.r) << 16) | (std::uint32_t(p.g) << 8) | p.b,
                                     (std::uint32_t(e.r) << 16) | (std::uint32_t(e.g) << 8) | e.b);
    if (seen.size() > 4096 || !seen.insert(key).second) return;
    std::fprintf(stderr, "COMBINER t=%.1fs w0=%08X w1=%08X cycle=%u tex=%d fmt=%u siz=%u blend=%d prim=%02X%02X%02X%02X env=%02X%02X%02X%02X geom=%08X\n",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
            state.combine_word0, state.combine_word1, unsigned((state.other_mode >> 52U) & 3U), state.textured ? 1 : 0,
            format, size, state.alpha_blend ? 1 : 0, p.r, p.g, p.b, p.a, e.r, e.g, e.b, e.a, state.geometry_mode);
}

} // namespace xr64::rage_wars::recomp
