#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

// This is deliberately a build-time control, not a runtime preference.  The
// clean performance target defines it to zero so diagnostic getenv calls,
// capture checks, and their call sites can be removed by the compiler.
#ifndef XR64_RENDER_DIAGNOSTICS
#define XR64_RENDER_DIAGNOSTICS 1
#endif

#if XR64_RENDER_DIAGNOSTICS
#define XR64_RENDER_DIAGNOSTIC_FLUSH() std::fflush(stderr)
#else
#define XR64_RENDER_DIAGNOSTIC_FLUSH() ((void)0)
#endif

namespace xr64::render_diagnostics {
// F9 selects one guest task; ordinary frames perform only a relaxed comparison.
inline std::atomic<std::uint64_t> capture_task{0};
inline bool capture_selected(std::uint64_t task) {
#ifdef XR64_DEMO_BUILD
    return false;
#else
    const auto selected = capture_task.load(std::memory_order_relaxed);
    return selected != 0 && selected == task;
#endif
}

#if XR64_RENDER_DIAGNOSTICS
inline bool enabled(const char *name) { const char *v = std::getenv(name); return v && v[0] == '1'; }
inline bool legacy() { static const bool v = enabled("XR64_RW_LEGACY_TRACE"); return v; }
inline bool readbacks() { static const bool v = enabled("XR64_RW_FRAMEBUFFER_PROBES"); return v; }
inline bool texture_capture() { static const bool v = enabled("XR64_RW_TEXTURE_CAPTURE"); return v; }
#else
constexpr bool enabled(const char *) { return false; }
constexpr bool legacy() { return false; }
constexpr bool readbacks() { return false; }
#ifdef XR64_DEMO_BUILD
constexpr bool texture_capture() { return false; }
#else
constexpr bool texture_capture() { return true; } // Still gated by the selected task.
#endif
#endif
struct Viewport { int x, y, width, height; };
inline Viewport fit_4_3(int width, int height) {
    width = std::max(0, width); height = std::max(0, height);
    const int w = std::min(width, height * 4 / 3);
    const int h = std::min(height, width * 3 / 4);
    return {(width - w) / 2, (height - h) / 2, w, h};
}
}
