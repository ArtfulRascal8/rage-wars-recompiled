#include "rage_wars_audio.hpp"
#include "rage_wars_audio_output.hpp"
#include "rage_wars_port_options.hpp"
#define NOMINMAX
#include <Windows.h>
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
namespace xr64::rage_wars::audio {
namespace {
class SdlBackend final : public Backend {
    HMODULE module_ = nullptr;
    bool initialized_ = false;
    int (*init_)(Uint32) = nullptr;
    SDL_AudioDeviceID (*open_)(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int) = nullptr;
    void (*close_)(SDL_AudioDeviceID) = nullptr;
    void (*pause_)(SDL_AudioDeviceID, int) = nullptr;
    int (*enqueue_)(SDL_AudioDeviceID, const void*, Uint32) = nullptr;
    Uint32 (*queued_)(SDL_AudioDeviceID) = nullptr;
    SDL_AudioStatus (*status_)(SDL_AudioDeviceID) = nullptr;
    const char* (*error_)() = nullptr;
public:
    ~SdlBackend() override { if (module_) FreeLibrary(module_); }
    bool initialize() override {
        if (initialized_) return true;
        if (!module_) module_ = LoadLibraryW(L"SDL2.dll");
        if (!module_) return false;
#define AUDIO_API(field, name) field = reinterpret_cast<decltype(field)>(GetProcAddress(module_, name)); if (!field) return false;
        AUDIO_API(init_, "SDL_InitSubSystem") AUDIO_API(open_, "SDL_OpenAudioDevice")
        AUDIO_API(close_, "SDL_CloseAudioDevice") AUDIO_API(pause_, "SDL_PauseAudioDevice")
        AUDIO_API(enqueue_, "SDL_QueueAudio") AUDIO_API(queued_, "SDL_GetQueuedAudioSize")
        AUDIO_API(status_, "SDL_GetAudioDeviceStatus") AUDIO_API(error_, "SDL_GetError")
#undef AUDIO_API
        initialized_ = init_(SDL_INIT_AUDIO) == 0;
        return initialized_;
    }
    Device open(std::uint32_t hz) override {
        SDL_AudioSpec wanted{}, obtained{};
        wanted.freq = static_cast<int>(hz); wanted.format = AUDIO_S16SYS;
        wanted.channels = 2; wanted.samples = 512;
        const auto id = open_(nullptr, 0, &wanted, &obtained, 0);
        // SDL starts a new queue device paused. No guest-format changes are permitted.
        if (id && (obtained.freq != static_cast<int>(hz) || obtained.format != AUDIO_S16SYS || obtained.channels != 2)) {
            close_(id); return {};
        }
        return {id, static_cast<std::uint32_t>(obtained.freq), obtained.samples};
    }
    void close(std::uint32_t id) override { close_(id); }
    void pause(std::uint32_t id, bool value) override { pause_(id, value ? 1 : 0); }
    int enqueue(std::uint32_t id, const std::int16_t* pcm, std::size_t count) override { return enqueue_(id, pcm, static_cast<Uint32>(count * sizeof(*pcm))); }
    std::size_t queued_frames(std::uint32_t id) override { return queued_(id) / 4; }
    bool stopped(std::uint32_t id) override { return status_(id) == SDL_AUDIO_STOPPED; }
    const char* error() override { return error_ ? error_() : "SDL2 load or symbol resolution failed"; }
};
SdlBackend backend;
Output output(backend);
std::mutex lifecycle_mutex, wake_mutex;
std::condition_variable wake;
std::jthread control, diagnostics;
std::atomic<bool> rate_requested{false};
thread_local bool submitted = true;
std::uint64_t monotonic_ms() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
}
bool enabled() {
#if defined(XR64_DEMO_BUILD) && defined(XR64_AUDIO)
    return true;
#elif defined(XR64_AUDIO)
    static const bool value = [] { const char* v = std::getenv("XR64_AUDIO"); return v && v[0] == '1'; }();
    return value;
#else
    return false;
#endif
}
void initialize() {
    if (!enabled()) return;
    std::lock_guard guard(lifecycle_mutex);
    if (control.joinable()) return;
    port_options::initialize(); // File I/O happens once on startup, never on PCM submission.
    telemetry::initialize("repaired-sdl2-queue");
    output.initial_gain(port_options::master_gain());
    control = std::jthread([](std::stop_token token) {
        auto report_at = monotonic_ms() + 5000;
        while (!token.stop_requested()) {
            const auto now = monotonic_ms();
            if (rate_requested.load(std::memory_order_acquire)) output.service(now);
            if (now >= report_at) { telemetry::flush(); report_at = now + 5000; }
            std::unique_lock lock(wake_mutex);
            wake.wait_for(lock, std::chrono::milliseconds(4));
        }
        output.shutdown(); telemetry::flush(); telemetry::export_capture();
    });
    const char* commands = std::getenv("XR64_AUDIO_CONTROL");
    if (telemetry::enabled() && commands && *commands) {
        diagnostics = std::jthread([](std::stop_token token) {
            while (!token.stop_requested()) {
                telemetry::poll_controls(&request_recovery);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
}
bool ready() { return enabled() && output.ready(); }
bool fifo_full() { return !enabled() || output.fifo_full(); }
std::uint32_t rate() { return output.rate(); }
void frequency(std::uint32_t hz) {
    if (!enabled() || hz < 8000 || hz > 96000) return;
    output.frequency(hz); rate_requested.store(true, std::memory_order_release); wake.notify_one();
}
std::size_t remaining() { return enabled() ? output.remaining() : 0; }
void begin_submission() noexcept { submitted = true; }
bool submission_succeeded() noexcept { return submitted; }
void queue(std::int16_t* samples, std::size_t count) {
    if (!enabled() || !count) return;
    float gain;
    { telemetry::Scope read(telemetry::Timer::gain_read); gain = port_options::master_gain(); }
    submitted = output.queue(samples, count, gain);
    if (output.state() == OutputState::priming || !submitted) wake.notify_one();
}
void device_removed(std::uint32_t id) { output.removed(id); wake.notify_one(); }
void request_recovery() { output.recover(); wake.notify_one(); }
void shutdown() {
    std::lock_guard guard(lifecycle_mutex);
    if(diagnostics.joinable()){diagnostics.request_stop();diagnostics.join();}
    output.stop_accepting();
    if (control.joinable()) { control.request_stop(); wake.notify_one(); control.join(); }
}
}
