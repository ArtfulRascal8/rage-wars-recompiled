#pragma once
#include <cstddef>
#include <cstdint>
namespace xr64::rage_wars::audio::telemetry {
enum class Timer { producer, rsp, output, submission_gap, submit_lock_wait, device_lock_wait, frame_interval, render_work, gain_read, presentation_interval, presentation_work, count };
enum class Context { menu, gameplay, intentional_pause, count };
enum class Counter { tasks, submitted_buffers, submitted_frames, invalid_buffers, queue_failures, queue_bound_rejections, unavailable_submissions, open_attempts, opens, open_failures, rate_changes, removals, recoveries, primed_starts, observed_empty_edges, priming_empty_observations, intentional_pauses, verified_buffers, nonzero_samples, source_mutations, unity_mismatches, count };
void initialize(const char* implementation);
bool enabled() noexcept;
std::uint64_t now_ns() noexcept;
void context(Context value) noexcept;
Context context() noexcept;
void increment(Counter counter, std::uint64_t amount = 1) noexcept;
void timing(Timer timer, std::uint64_t nanoseconds, Context context) noexcept;
void queue_depth(std::size_t frames, bool priming, bool intentional_pause) noexcept;
void device(const char* state, std::uint32_t rate, std::size_t quantum, std::size_t target, std::size_t bound, const char* error = "");
void pcm(const std::int16_t* source, const std::int16_t* converted, std::size_t count, bool settled_unity, std::uint64_t source_hash) noexcept;
std::uint64_t verification_hash(const std::int16_t* samples, std::size_t count) noexcept;
bool verify_next_buffer() noexcept;
Context guest_context(const std::uint8_t* rdram) noexcept;
void frame(Context context, std::uint64_t render_ns, bool independent) noexcept; // Guest graphics-task intervals/decode work.
void presentation(Context context, std::uint64_t work_ns) noexcept; // Completed desktop presentation, including repeats.
void input_observation(bool armed, bool save_ready, std::uint16_t buttons, bool stick) noexcept;
void poll_controls(void (*recover)()); // Optional validation controller, off all audio paths.
void capture(const std::int16_t* pcm, std::size_t count, std::uint32_t rate, std::size_t queued_before) noexcept;
void export_capture(); // Only after producer quiescence at shutdown.
void flush(); // Control/shutdown threads only; never a PCM or RSP hook.
class Scope {
    Timer timer_;
    Context context_;
    std::uint64_t start_;
public:
    explicit Scope(Timer timer) noexcept : timer_(timer), context_(context()), start_(now_ns()) {}
    ~Scope() { if (start_) timing(timer_, now_ns() - start_, context_); }
};
}
