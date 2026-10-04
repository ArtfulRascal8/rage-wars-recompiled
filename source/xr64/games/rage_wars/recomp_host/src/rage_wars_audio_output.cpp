#include "rage_wars_audio_output.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace xr64::rage_wars::audio {
namespace {
using telemetry::Counter;
using telemetry::Timer;
std::int16_t scale(std::int16_t sample, float gain) {
    return static_cast<std::int16_t>(std::clamp(std::round(static_cast<float>(sample) * gain), -32768.0F, 32767.0F));
}
bool usable(OutputState state) { return state == OutputState::priming || state == OutputState::playing; }
}
void GainRamp::convert(const std::int16_t* source, std::size_t count, std::int16_t* dest, float target, std::uint32_t rate) noexcept {
    if (target != target_) {
        target_ = target; left_ = std::max<std::size_t>(1, rate / 100);
        step_ = (target_ - current_) / static_cast<float>(left_);
    }
    // Word-swapped RDRAM stores host samples right,left. Settled unity is bit-exact.
    if (settled_unity()) {
        for (std::size_t i = 0; i < count; i += 2) { dest[i] = source[i + 1]; dest[i + 1] = source[i]; }
        return;
    }
    for (std::size_t i = 0; i < count; i += 2) {
        if (left_) { current_ += step_; if (!--left_) current_ = target_; }
        dest[i] = scale(source[i + 1], current_); dest[i + 1] = scale(source[i], current_);
    }
}
void Output::frequency(std::uint32_t hz) noexcept {
    if (hz < 8000 || hz > 96000) return;
    if (desired_rate_.exchange(hz, std::memory_order_acq_rel) != hz) telemetry::increment(Counter::rate_changes);
}
void Output::fault_locked(const char* reason) {
    std::strncpy(error_.data(), reason ? reason : "device failure", error_.size() - 1); error_.back() = 0;
    ++recovery_serial_;
    state_.store(OutputState::recovering, std::memory_order_release);
}
std::size_t Output::observe_locked() {
    const auto frames = backend_.queued_frames(device_.id);
    const bool priming = state() == OutputState::priming;
    telemetry::queue_depth(frames, priming, false);
    if (!priming && seen_pcm_) {
        if (!frames && !empty_) telemetry::increment(Counter::observed_empty_edges);
        empty_ = !frames;
    }
    return frames;
}
bool Output::ready() {
    return accepting_.load(std::memory_order_acquire) && usable(state());
}
bool Output::fifo_full() {
    if (!ready()) return true; // Backpressure during loss, without an invented completion clock.
    const auto start = telemetry::now_ns();
    std::lock_guard guard(device_mutex_);
    if (start) telemetry::timing(Timer::device_lock_wait, telemetry::now_ns() - start, telemetry::context());
    if (!device_.id || !usable(state()) || device_.rate != rate()) return true;
    // Identical priming/fullness thresholds: whole submissions may cross the target,
    // so a paused device cannot become full before the priming margin is reachable.
    return observe_locked() >= target_;
}
std::size_t Output::remaining() {
    std::lock_guard guard(device_mutex_);
    return device_.id && usable(state()) ? observe_locked() : 0;
}
bool Output::queue(const std::int16_t* samples, std::size_t count, float target_gain) {
    telemetry::Scope output(Timer::output);
    if (!samples || !count || (count & 1) || count > pcm_.size()) { telemetry::increment(Counter::invalid_buffers); return false; }
    const auto wait = telemetry::now_ns();
    std::lock_guard submit(submit_mutex_);
    if (wait) telemetry::timing(Timer::submit_lock_wait, telemetry::now_ns() - wait, telemetry::context());
    if (!ready()) { telemetry::increment(Counter::unavailable_submissions); return false; }
    const auto hz = rate();
    const bool verify = telemetry::verify_next_buffer();
    const auto hash = verify ? telemetry::verification_hash(samples, count) : 0;
    const bool unity = gain_.settled_unity() && target_gain == 1.0F;
    gain_.convert(samples, count, pcm_.data(), target_gain, hz);
    if (verify) telemetry::pcm(samples, pcm_.data(), count, unity, hash);
    const auto gate_wait = telemetry::now_ns();
    std::lock_guard gate(device_mutex_);
    if (gate_wait) telemetry::timing(Timer::device_lock_wait, telemetry::now_ns() - gate_wait, telemetry::context());
    if (!ready() || !device_.id || device_.rate != hz || hz != rate()) { telemetry::increment(Counter::unavailable_submissions); return false; }
    const auto before = observe_locked();
    if (before >= target_) { telemetry::increment(Counter::queue_bound_rejections); return false; }
    if (backend_.enqueue(device_.id, pcm_.data(), count) != 0) {
        telemetry::increment(Counter::queue_failures); fault_locked(backend_.error()); return false;
    }
    telemetry::capture(pcm_.data(),count,hz,before);
    seen_pcm_ = true; telemetry::increment(Counter::submitted_buffers); telemetry::increment(Counter::submitted_frames, count / 2);
    const auto now = telemetry::now_ns();
    const auto context = telemetry::context();
    if (now && last_submission_ns_ && context == last_submission_context_)
        telemetry::timing(Timer::submission_gap, now - last_submission_ns_, context);
    last_submission_ns_ = now; last_submission_context_ = context; observe_locked(); return true;
}
void Output::removed(std::uint32_t id) {
    std::lock_guard guard(device_mutex_);
    if (device_.id == id && id) { telemetry::increment(Counter::removals); fault_locked("SDL audio device removed"); }
}
void Output::recover() { std::lock_guard guard(device_mutex_); fault_locked("explicit diagnostic device recovery"); }
void Output::service(std::uint64_t ms) {
    if (!accepting_.load(std::memory_order_acquire)) return;
    Device retiring{}, playing{}; bool lost = false, handled = false;
    std::size_t playing_target = 0; std::array<char, 256> reason{};
    {
        std::lock_guard guard(device_mutex_);
        if (device_.id && usable(state()) && backend_.stopped(device_.id)) fault_locked("SDL audio device stopped");
        if (device_.id && (state() == OutputState::recovering || device_.rate != rate())) {
            lost = state() == OutputState::recovering; retiring = device_; device_ = {};
            if (lost) reason = error_;
            else std::strncpy(reason.data(), "guest sample rate changed", reason.size() - 1);
            last_submission_ns_ = 0; seen_pcm_ = empty_ = false;
            state_.store(OutputState::recovering, std::memory_order_release);
        } else if (device_.id) {
            const auto frames = observe_locked();
            if (state() == OutputState::priming && frames >= target_) {
                backend_.pause(device_.id, false); // Control owner only.
                state_.store(OutputState::playing, std::memory_order_release); telemetry::increment(Counter::primed_starts);
                playing = device_; playing_target = target_;
            }
            handled = true;
        }
    }
    // Reporting may allocate or write; it never holds a producer/device gate.
    if (playing.id) telemetry::device("playing", playing.rate, playing.quantum, playing_target, playing_target + kMaxSubmissionFrames - 1);
    if (handled) return;
    if (retiring.id) {
        backend_.close(retiring.id); // May block; neither producer nor device gate is held.
        next_retry_ms_ = lost ? ms + 250 : ms;
        telemetry::device("recovering", rate(), 0, 0, 0, reason.data());
    }
    if (ms < next_retry_ms_) return;
    const auto requested = rate(); std::uint64_t opening_serial;
    {
        std::lock_guard gate(device_mutex_); opening_serial = recovery_serial_;
        state_.store(OutputState::opening, std::memory_order_release);
    }
    telemetry::increment(Counter::open_attempts);
    if (!initialized_) initialized_ = backend_.initialize(); // Initialization failures remain retryable.
    Device opened = initialized_ ? backend_.open(requested) : Device{};
    if (!opened.id) {
        telemetry::increment(Counter::open_failures); telemetry::device("recovering", requested, 0, 0, 0, backend_.error());
        state_.store(OutputState::recovering, std::memory_order_release);
        next_retry_ms_ = ms + retry_delay_ms_; retry_delay_ms_ = std::min(2000U, retry_delay_ms_ * 2); return;
    }
    bool publish = false; std::size_t opened_target = 0;
    {
        std::lock_guard guard(device_mutex_);
        if (accepting_.load(std::memory_order_acquire) && opening_serial == recovery_serial_ && requested == rate() && opened.rate == requested && opened.quantum) {
            device_ = opened;
            // Selected fixed initial margin: three SDL quanta; no adaptive latency growth.
            // The guest fullness adapter uses this same reachable target.
            target_ = std::max<std::size_t>((requested + 29) / 30, opened.quantum * 3);
            opened_target = target_;
            last_submission_ns_ = 0; seen_pcm_ = empty_ = false;
            state_.store(OutputState::priming, std::memory_order_release);
            publish = true;
        }
    }
    if (!publish) { backend_.close(opened.id); state_.store(OutputState::recovering, std::memory_order_release); return; }
    telemetry::device("priming", requested, opened.quantum, opened_target, opened_target + kMaxSubmissionFrames - 1);
    telemetry::increment(Counter::opens); if (had_device_) telemetry::increment(Counter::recoveries);
    had_device_ = true; retry_delay_ms_ = 250; next_retry_ms_ = 0;
}
void Output::shutdown() {
    stop_accepting(); Device retiring{};
    {
        std::lock_guard guard(device_mutex_); retiring = device_; device_ = {};
        state_.store(OutputState::stopped, std::memory_order_release);
    }
    if (retiring.id) backend_.close(retiring.id);
    { std::lock_guard guard(submit_mutex_); } // Wait for any in-flight conversion before final reporting.
    telemetry::device("stopped", rate(), 0, 0, 0);
}
}
