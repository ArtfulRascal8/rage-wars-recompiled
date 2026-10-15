#define NOMINMAX
#include "rage_wars_model_filter.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
using namespace xr64;
using namespace xr64::rage_wars::recomp;

int main(int argc, char** argv) {
    const bool negative = argc == 2 && std::strcmp(argv[1], "--nearest-control") == 0;
    N64RawFast3DDrawState state;
    state.textured = true; state.geometry_mode = 0x230405;
    state.other_mode = 0x0018AC100C192438ULL;
    state.combine_word0 = 0xFC1217FF; state.combine_word1 = 0xFFFFFE38;
    if (!captured_model_filter_material(state, 2, 0) ||
            !captured_model_filter_material(state, 2, 1)) return 2;
    for (unsigned change = 0; change < 8; ++change) {
        auto unsupported = state; unsigned format = 2, size = 1;
        switch (change) {
        case 0: unsupported.textured = false; break;
        case 1: format = 0; break;
        case 2: size = 2; break;
        case 3: unsupported.geometry_mode &= ~0x20000U; break;
        case 4: unsupported.geometry_mode |= 0x40000U; break;
        case 5: unsupported.other_mode &= ~(3ULL << 44); break;
        case 6: unsupported.other_mode ^= 1ULL << 52; break;
        case 7: unsupported.combine_word1 ^= 1; break;
        }
        if (captured_model_filter_material(unsupported, format, size)) return 2;
    }
    CapturedModelFilter filter;
    if (filter.begin(2, 2)) return 2; // No GL context is a real unavailable state.
    WNDCLASSA wc{};
    wc.style = CS_OWNDC; wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "XR64ModelFilterContract";
    if (!RegisterClassA(&wc)) return 2;
    const HWND window = CreateWindowExA(0, wc.lpszClassName, "XR64 GPU contract",
            WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 2; // Hidden fixture window; never steals foreground.
    const HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cAlphaBits = 8;
    const int pixel_format = ChoosePixelFormat(dc, &pfd);
    if (!pixel_format || !SetPixelFormat(dc, pixel_format, &pfd)) return 2;
    const HGLRC context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) return 2;
    std::printf("GPU=%s GL=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    glViewport(0, 0, 64, 64);
    glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    const unsigned char pixels[16] = {255,0,0,128, 0,255,0,255, 0,0,255,0, 255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    if (!filter.begin(2, 2)) { std::fprintf(stderr, "%s\n", filter.reason().c_str()); return 2; }
    GLint own_program = 0; glGetIntegerv(0x8B8D, &own_program); filter.end();
    const auto use_program = reinterpret_cast<void (APIENTRY *)(GLuint)>(wglGetProcAddress("glUseProgram"));
    int failures = 0, checks = 0;
    const auto sample = [&](const char* name, float s, float t, GLint wrap,
            std::array<int,4> expected, float q = 1, float shade = 1, float alpha = 1) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
        if (!negative && !filter.begin(2,2)) { ++failures; return; }
        glColor4f(shade, shade, shade, alpha);
        glTexCoord4f(s/2*q, t/2*q, 0, q);
        glBegin(GL_TRIANGLES);
        glVertex2f(-1,-1); glVertex2f(3,-1); glVertex2f(-1,3);
        glEnd();
        if (!negative) filter.end();
        std::array<unsigned char,4> result{};
        glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
        GLint restored = -1; glGetIntegerv(0x8B8D, &restored);
        bool ok = restored == 0 && glGetError() == GL_NO_ERROR;
        for (unsigned c=0;c<4;++c) ok = ok && std::abs(int(result[c])-expected[c])<=1;
        ++checks; if (!ok) ++failures;
        std::printf("%s %s actual=%u,%u,%u,%u expected=%d,%d,%d,%d\n",
                ok?"PASS":"FAIL",name,result[0],result[1],result[2],result[3],
                expected[0],expected[1],expected[2],expected[3]);
    };
    constexpr GLint clamp = 0x812F, repeat = GL_REPEAT, mirror = 0x8370;
    sample("integer_center",0,0,clamp,{255,0,0,128});
    sample("lower_triangle",.25F,.25F,clamp,{128,64,64,128});
    sample("upper_triangle",.75F,.75F,clamp,{128,191,191,191});
    sample("diagonal",.5F,.5F,clamp,{0,128,128,128});
    sample("five_bit_fraction",.28F,.28F,clamp,{128,64,64,128});
    sample("negative_repeat",-.25F,.25F,repeat,{255,64,64,160});
    sample("negative_clamp",-.25F,.25F,clamp,{191,0,64,96});
    sample("mirror_edge",1.75F,.25F,mirror,{64,255,64,255});
    sample("repeat_boundary",2,0,repeat,{255,0,0,128});
    sample("clamp_boundary",2,0,clamp,{0,255,0,255});
    sample("perspective_q",.25F,.25F,clamp,{128,64,64,128},.125F);
    sample("shade_and_alpha",.25F,.25F,clamp,{64,32,32,64},1,.5F,.5F);
    if (!negative && !filter.begin(2,2)) return 2;
    glColor4f(1,1,1,1);
    glBegin(GL_TRIANGLES);
    glTexCoord4f(0,0,0,1); glVertex2f(-1,-1);
    glTexCoord4f(.125F,0,0,.25F); glVertex2f(3,-1);
    glTexCoord4f(0,.25F,0,.5F); glVertex2f(-1,3);
    glEnd();
    if (!negative) filter.end();
    std::array<unsigned char,4> projective{};
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,projective.data());
    const std::array<int,4> projected_expected{231,8,16,124};
    bool projective_ok = true;
    for (unsigned c=0;c<4;++c)
        projective_ok = projective_ok && std::abs(int(projective[c])-projected_expected[c])<=1;
    ++checks; if (!projective_ok) ++failures;
    std::printf("%s varying_perspective_q actual=%u,%u,%u,%u expected=231,8,16,124\n",
            projective_ok?"PASS":"FAIL",projective[0],projective[1],projective[2],projective[3]);
    // A new atlas shape must update the context-local size uniform.
    const unsigned char row[16] = {0,0,0,255, 64,0,0,255, 128,0,0,255, 255,0,0,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,1,0,GL_RGBA,GL_UNSIGNED_BYTE,row);
    if (!negative && !filter.begin(4,1)) return 2;
    glColor4f(1,1,1,1); glTexCoord4f(1.25F/4,0,0,1);
    glBegin(GL_TRIANGLES); glVertex2f(-1,-1); glVertex2f(3,-1); glVertex2f(-1,3); glEnd();
    if (!negative) filter.end();
    std::array<unsigned char,4> resized{};
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,resized.data());
    const bool resized_ok = std::abs(int(resized[0])-80)<=1 && resized[1]==0 && resized[2]==0 && resized[3]==255;
    ++checks; if (!resized_ok) ++failures;
    std::printf("%s atlas_shape_change actual=%u,%u,%u,%u expected=80,0,0,255\n",
            resized_ok?"PASS":"FAIL",resized[0],resized[1],resized[2],resized[3]);
    // Incoming shader ownership must survive the scoped override, including
    // repeated/nested begin rejection. Use an actual linked program sentinel.
    use_program(static_cast<GLuint>(own_program));
    if (!filter.begin(2,2) || filter.begin(2,2)) ++failures;
    filter.end();
    GLint restored = 0; glGetIntegerv(0x8B8D, &restored);
    if (restored != own_program) ++failures;
    use_program(0);
    filter.release_current_context();
    if (!filter.begin(2,2)) ++failures; else filter.end(); // Context-local reinitialization.
    filter.release_current_context();
    glDeleteTextures(1,&texture); wglMakeCurrent(nullptr,nullptr); wglDeleteContext(context);
    ReleaseDC(window,dc); DestroyWindow(window); UnregisterClassA(wc.lpszClassName,wc.hInstance);
    std::printf("model_filter checks=%d failures=%d control=%s\n",checks,failures,negative?"nearest":"three_point");
    return failures ? 1 : 0;
}
