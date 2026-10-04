#pragma once
#include "rage_wars_audio_telemetry.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
namespace xr64::rage_wars::audio {
constexpr std::size_t kMaxSubmissionFrames = 4096;
enum class OutputState { starting, opening, priming, playing, recovering, stopped };
struct Device { std::uint32_t id = 0, rate = 0; std::size_t quantum = 0; };
// SDL owns a copy after enqueue returns. No backend operation retains guest pointers.
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool initialize() = 0;
    virtual Device open(std::uint32_t rate) = 0;
    virtual void close(std::uint32_t device) = 0;
    virtual void pause(std::uint32_t device, bool paused) = 0;
    virtual int enqueue(std::uint32_t device, const std::int16_t* pcm, std::size_t count) = 0;
    virtual std::size_t queued_frames(std::uint32_t device) = 0;
    virtual bool stopped(std::uint32_t device) = 0;
    virtual const char* error() = 0;
};
class GainRamp {
    float current_ = 1.0F, target_ = 1.0F, step_ = 0.0F;
    std::size_t left_ = 0;
public:
    void reset(float gain) noexcept { current_ = target_ = gain; step_ = 0; left_ = 0; }
    bool settled_unity() const noexcept { return current_ == 1.0F && target_ == 1.0F && !left_; }
    float current() const noexcept { return current_; }
    void convert(const std::int16_t* source, std::size_t count, std::int16_t* destination, float target, std::uint32_t rate) noexcept;
};
class Output {
    Backend& backend_;
    // Conversion/order use a separate gate. Opening and closing never hold either mutex.
    std::mutex submit_mutex_, device_mutex_;
    std::array<std::int16_t, kMaxSubmissionFrames * 2> pcm_{};
    GainRamp gain_;
    std::atomic<OutputState> state_{OutputState::starting};
    std::atomic<std::uint32_t> desired_rate_{32000};
    std::atomic<bool> accepting_{true};
    Device device_{}; // Device fields, thresholds and observations use device_mutex_.
    std::uint64_t last_submission_ns_ = 0, recovery_serial_ = 0;
    std::size_t target_ = 0;
    telemetry::Context last_submission_context_ = telemetry::Context::menu;
    bool empty_ = false, seen_pcm_ = false;
    std::array<char, 256> error_{};
    std::uint64_t next_retry_ms_ = 0;
    unsigned retry_delay_ms_ = 250;
    bool initialized_ = false, had_device_ = false;
    void fault_locked(const char* error);
    std::size_t observe_locked();
public:
    explicit Output(Backend& backend) : backend_(backend) {}
    OutputState state() const noexcept { return state_.load(std::memory_order_acquire); }
    void initial_gain(float value) { std::lock_guard guard(submit_mutex_); gain_.reset(value); }
    void frequency(std::uint32_t hz) noexcept;
    std::uint32_t rate() const noexcept { return desired_rate_.load(std::memory_order_acquire); }
    bool ready();
    bool fifo_full();
    std::size_t remaining();
    bool queue(const std::int16_t* samples, std::size_t count, float gain);
    void removed(std::uint32_t id);
    void recover();
    void stop_accepting() noexcept { accepting_.store(false, std::memory_order_release); }
    // Single control owner; PCM hooks never initialize, open, close or unpause SDL.
    void service(std::uint64_t monotonic_ms);
    void shutdown(); // Control owner after stop_accepting, then join it.
};
}
