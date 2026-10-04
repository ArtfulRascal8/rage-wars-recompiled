#include "rage_wars_audio_telemetry.hpp"
#include "rage_wars_port_options.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
namespace xr64::rage_wars::audio::telemetry {
namespace {
constexpr std::size_t kBins = 4001;
constexpr auto kTimers = static_cast<std::size_t>(Timer::count);
constexpr auto kContexts = static_cast<std::size_t>(Context::count);
constexpr auto kCounters = static_cast<std::size_t>(Counter::count);
struct Times {
    std::atomic<std::uint64_t> count{0}, total{0}, maximum{0};
    std::array<std::atomic<std::uint64_t>, kBins> bins{};
};
std::array<std::array<Times, kTimers>, kContexts> times;
std::array<std::atomic<std::uint64_t>, kCounters> counters{};
std::atomic<bool> active{false};
std::atomic<Context> current{Context::menu};
std::atomic<std::uint64_t> queue_min{UINT64_MAX}, queue_max{0}, queue_observations{0};
std::atomic<std::uint64_t> peak{0}, verification_claims{0}, largest_submission{0}, smallest_submission{UINT64_MAX};
std::array<std::atomic<std::uint64_t>, kContexts> empty_edges{};
std::atomic<std::uint64_t> control_id{0},input_calls{0},input_nonzero{0},input_first_armed_ns{0};
std::atomic<bool> input_armed{false},save_ready{false},independent_presentation{false};
std::atomic<std::uint16_t> button_union{0};
std::string control_path, capture_path;
std::vector<std::int16_t> capture_samples;
struct CaptureStamp { std::uint64_t elapsed_ns; std::size_t offset, samples, queued_before; };
std::array<CaptureStamp, 1024> capture_stamps{};
std::size_t capture_size=0,capture_stamp_count=0;
std::uint32_t capture_rate=0;
std::uint64_t capture_after_ns=0,verify_after_ns=0,start_ns=0;
bool verify = false;
std::mutex report_mutex;
std::string report_path, implementation, workload, state = "not_started", last_error;
std::uint32_t sample_rate = 0;
std::size_t device_quantum = 0, prime_target = 0, queue_bound = 0;
std::chrono::steady_clock::time_point started;
void maximum(std::atomic<std::uint64_t>& counter, std::uint64_t value) {
    auto old = counter.load(std::memory_order_relaxed);
    while (old < value && !counter.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}
std::uint64_t bin_width(Timer timer) {
    return timer == Timer::frame_interval || timer == Timer::submission_gap || timer == Timer::presentation_interval ? 100000 : 25000;
}
std::string quoted(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c >= 32) out += static_cast<char>(c);
    }
    return out + '"';
}
constexpr const char* timer_names[] = {"producer", "rsp", "output", "submission_gap", "submit_lock_wait", "device_lock_wait", "frame_interval", "render_work", "gain_read", "presentation_interval", "presentation_work"};
constexpr const char* context_names[] = {"menu", "gameplay", "guest_pause"};
constexpr const char* counter_names[] = {"tasks", "submitted_buffers", "submitted_frames", "invalid_buffers", "queue_failures", "queue_bound_rejections", "unavailable_submissions", "open_attempts", "opens", "open_failures", "rate_changes", "removals", "recoveries", "primed_starts", "observed_empty_edges", "priming_empty_observations", "intentional_pauses", "verified_buffers", "nonzero_samples", "source_mutations", "unity_mismatches"};
}
void initialize(const char* name) {
    const char* path = std::getenv("XR64_AUDIO_REPORT");
    const char* check = std::getenv("XR64_AUDIO_VERIFY_PCM");
    const char* label = std::getenv("XR64_AUDIO_WORKLOAD");
    implementation = name; report_path = path ? path : ""; workload = label ? label : "unspecified";
    verify = check && check[0] == '1'; started = std::chrono::steady_clock::now();
    const char* controller=std::getenv("XR64_AUDIO_CONTROL");control_path=controller?controller:"";
    const char* capture=std::getenv("XR64_AUDIO_CAPTURE");capture_path=capture?capture:"";
    const char* verify_after=std::getenv("XR64_AUDIO_VERIFY_AFTER_SECONDS");
    verify_after_ns=verify_after?static_cast<std::uint64_t>(std::clamp(std::atoi(verify_after),0,600))*1000000000ULL:0;
    const char* after=std::getenv("XR64_AUDIO_CAPTURE_AFTER_SECONDS");
    capture_after_ns=after?static_cast<std::uint64_t>(std::clamp(std::atoi(after),0,600))*1000000000ULL:0;
    if(!capture_path.empty())capture_samples.resize(96000*2*2); // Bounded two-second capture, allocated at startup.
    start_ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(started.time_since_epoch()).count());
    active.store(!report_path.empty(), std::memory_order_release);
}
bool enabled() noexcept { return active.load(std::memory_order_relaxed); }
std::uint64_t now_ns() noexcept {
    if (!enabled()) return 0;
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void context(Context value) noexcept {
    const auto old=current.exchange(value,std::memory_order_relaxed);
    if(value==Context::intentional_pause && old!=value)increment(Counter::intentional_pauses);
}
Context context() noexcept { return current.load(std::memory_order_relaxed); }
void increment(Counter value, std::uint64_t amount) noexcept {
    if (!enabled())return;
    counters[static_cast<std::size_t>(value)].fetch_add(amount, std::memory_order_relaxed);
    if(value==Counter::observed_empty_edges)empty_edges[static_cast<std::size_t>(context())].fetch_add(amount,std::memory_order_relaxed);
    if(value==Counter::submitted_frames) {
        maximum(largest_submission,amount);
        auto old=smallest_submission.load(std::memory_order_relaxed);
        while(amount<old&&!smallest_submission.compare_exchange_weak(old,amount,std::memory_order_relaxed)){}
    }
}
void timing(Timer timer, std::uint64_t ns, Context ctx) noexcept {
    if (!enabled()) return;
    auto& stat = times[static_cast<std::size_t>(ctx)][static_cast<std::size_t>(timer)];
    stat.count.fetch_add(1, std::memory_order_relaxed); stat.total.fetch_add(ns, std::memory_order_relaxed);
    maximum(stat.maximum, ns);
    stat.bins[std::min<std::size_t>(ns / bin_width(timer), kBins - 1)].fetch_add(1, std::memory_order_relaxed);
}
void queue_depth(std::size_t frames, bool priming, bool paused) noexcept {
    if (!enabled()) return;
    if (priming) { if (!frames) increment(Counter::priming_empty_observations); return; }
    if (paused) return;
    queue_observations.fetch_add(1, std::memory_order_relaxed);
    auto old = queue_min.load(std::memory_order_relaxed);
    while (frames < old && !queue_min.compare_exchange_weak(old, frames, std::memory_order_relaxed)) {}
    maximum(queue_max, frames);
}
void device(const char* value, std::uint32_t rate, std::size_t quantum, std::size_t target, std::size_t bound, const char* error) {
    std::lock_guard guard(report_mutex);
    if(state!=value || sample_rate!=rate) {
        const std::string message=std::string("Audio output: ")+value+"; guest rate "+std::to_string(rate)+
            (error&&*error?std::string("; ")+error:"")+"\n";
        std::fwrite(message.data(),1,message.size(),stderr);
    }
    state = value; sample_rate = rate;
    if (quantum) { device_quantum = quantum; prime_target = target; queue_bound = bound; }
    if (error && *error) last_error = error;
}
std::uint64_t verification_hash(const std::int16_t* source, std::size_t count) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < count; ++i) { hash ^= static_cast<std::uint16_t>(source[i]); hash *= 1099511628211ULL; }
    return hash;
}
bool verify_next_buffer() noexcept { return enabled() && verify && now_ns()-start_ns>=verify_after_ns && verification_claims.fetch_add(1, std::memory_order_relaxed) < 128; }
void pcm(const std::int16_t* source, const std::int16_t* converted, std::size_t count, bool unity, std::uint64_t hash) noexcept {
    increment(Counter::verified_buffers);
    if (hash != verification_hash(source, count)) increment(Counter::source_mutations);
    std::uint64_t nonzero = 0, local_peak = 0, mismatch = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = std::abs(static_cast<int>(converted[i]));
        nonzero += value != 0; local_peak = std::max<std::uint64_t>(local_peak, value);
        if (unity && converted[i] != source[i ^ 1]) ++mismatch;
    }
    increment(Counter::nonzero_samples, nonzero); increment(Counter::unity_mismatches, mismatch); maximum(peak, local_peak);
}
Context guest_context(const std::uint8_t* ram) noexcept {
    if(!ram || ram[0x140225U^3U]!=1)return Context::menu;
    std::uint32_t pause=0,menu=0;
    std::memcpy(&pause,ram+0x1407D4U,sizeof(pause));
    std::memcpy(&menu,ram+0x144E00U,sizeof(menu));
    return pause?Context::intentional_pause:menu?Context::menu:Context::gameplay;
}
void frame(Context ctx, std::uint64_t render_ns, bool independent) noexcept {
    independent_presentation.store(independent, std::memory_order_relaxed);
    if (!enabled()) return;
    // The guest render owner calls this; XR repeat-presentation does not count as new frames.
    static thread_local std::uint64_t previous = 0;
    static thread_local Context previous_context = Context::menu;
    context(ctx);
    const auto now = now_ns();
    // A resume/scene transition must not label the preceding pause as a gameplay gap.
    if (previous && previous_context == ctx) timing(Timer::frame_interval, now - previous, ctx);
    previous = now; previous_context = ctx; timing(Timer::render_work, render_ns, ctx);
}
void presentation(Context ctx, std::uint64_t work_ns) noexcept {
    if (!enabled()) return;
    static thread_local std::uint64_t previous = 0;
    static thread_local Context previous_context = Context::menu;
    const auto now = now_ns();
    if (previous && previous_context == ctx) timing(Timer::presentation_interval, now - previous, ctx);
    previous = now; previous_context = ctx;
    timing(Timer::presentation_work, work_ns, ctx);
}
void flush() {
    if (!enabled()) return;
    std::lock_guard guard(report_mutex);
    std::ostringstream out; out.precision(9);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    out << "{\n\"schema\":1,\"implementation\":" << quoted(implementation) << ",\"workload\":" << quoted(workload)
        << ",\"elapsed_seconds\":" << seconds << ",\"task_rate_hz\":" << counters[static_cast<std::size_t>(Counter::tasks)].load() / std::max(seconds, 0.001)
        << ",\"device_state\":" << quoted(state) << ",\"last_device_error\":" << quoted(last_error)
        << ",\"guest_rate_hz\":" << sample_rate << ",\"device_quantum_frames\":" << device_quantum
        << ",\"priming_target_frames\":" << prime_target << ",\"queue_bound_frames\":" << queue_bound
        << ",\"queue_min_frames\":" << (queue_observations.load() ? queue_min.load() : 0)
        << ",\"queue_max_frames\":" << queue_max.load() << ",\"queue_observations\":" << queue_observations.load()
        << ",\"verified_pcm_peak\":" << peak.load() << ",\"largest_submission_frames\":" << largest_submission.load()
        << ",\"smallest_submission_frames\":" << (smallest_submission.load()==UINT64_MAX?0:smallest_submission.load())
        << ",\"independent_presentation\":" << (independent_presentation.load()?"true":"false")
        << ",\"input_replay_armed\":" << (input_armed.load()?"true":"false") << ",\"save_profile_ready\":" << (save_ready.load()?"true":"false")
        << ",\"input_armed_after_seconds\":" << (input_first_armed_ns.load() ? (input_first_armed_ns.load()-start_ns)/1.0e9 : -1.0)
        << ",\"input_calls\":" << input_calls.load() << ",\"input_nonzero_calls\":" << input_nonzero.load() << ",\"input_button_union\":" << button_union.load()
        << ",\"last_control_id\":" << control_id.load()
        << ",\"observed_empty_edges_by_context\":{\"menu\":" << empty_edges[0].load() << ",\"gameplay\":" << empty_edges[1].load()
        << ",\"guest_pause\":" << empty_edges[2].load() << "},\"counters\":{";
    for (std::size_t i = 0; i < kCounters; ++i) { if (i) out << ','; out << quoted(counter_names[i]) << ':' << counters[i].load(); }
    out << "},\n\"timings\":{";
    for (std::size_t c = 0; c < kContexts; ++c) {
        if (c) out << ','; out << quoted(context_names[c]) << ":{";
        for (std::size_t t = 0; t < kTimers; ++t) {
            if (t) out << ','; auto& stat = times[c][t]; const auto count = stat.count.load();
            out << quoted(timer_names[t]) << ":{\"count\":" << count << ",\"mean_us\":" << (count ? stat.total.load() / (1000.0 * count) : 0) << ",\"max_us\":" << stat.maximum.load() / 1000.0;
            for (unsigned percentile : {50U, 95U, 99U}) {
                const auto rank = (count * percentile + 99) / 100; std::uint64_t sum = 0; std::size_t b = 0;
                if (count) for (; b < kBins - 1; ++b) { sum += stat.bins[b].load(); if (sum >= rank) break; }
                out << ",\"p" << percentile << "_bucket_upper_us\":";
                if (b == kBins - 1) out << "null"; else out << (count ? (b + 1) * bin_width(static_cast<Timer>(t)) / 1000.0 : 0);
            }
            out << '}';
        }
        out << '}';
    }
    out << "},\n\"measurement_limits\":\"Queue-empty edges are observations, not confirmed audible underruns. SDL queue excludes hardware-owned audio. frame_interval/render_work measure guest graphics-task intervals/decode; presentation_interval/work measure completed desktop presentations including repeats. Percentiles are histogram upper bounds. Producer time includes host scheduling waits; RSP/output timers may be nested.\"\n}\n";
    // One aggregate report, at most every five seconds and shutdown, off the audio path.
    std::ofstream file(report_path, std::ios::binary | std::ios::trunc); file << out.str();
}

void input_observation(bool armed,bool saved,std::uint16_t buttons,bool stick) noexcept {
    if(!enabled())return;
    if(armed && !input_first_armed_ns.load(std::memory_order_relaxed))input_first_armed_ns.store(now_ns(),std::memory_order_relaxed);
    input_armed.store(armed);save_ready.store(saved);input_calls.fetch_add(1,std::memory_order_relaxed);
    if(buttons || stick)input_nonzero.fetch_add(1,std::memory_order_relaxed);
    button_union.fetch_or(buttons,std::memory_order_relaxed);
}
void poll_controls(void (*recover)()) {
    if(!enabled() || control_path.empty())return;
    std::ifstream file(control_path);
    std::uint64_t id=0;std::string action;float value=0;
    if(!(file>>id>>action) || id<=control_id.load())return;
    if(action=="gain" && (file>>value) && std::isfinite(value) && value>=0 && value<=1)
        port_options::set_master_volume(value);
    else if(action=="recover")recover();
    else return;
    control_id.store(id,std::memory_order_release);
}
void capture(const std::int16_t* pcm,std::size_t count,std::uint32_t rate,std::size_t queued_before) noexcept {
    if(capture_samples.empty() || !enabled())return;
    const auto elapsed=now_ns()-start_ns;
    if(elapsed<capture_after_ns || capture_stamp_count>=capture_stamps.size())return;
    if(!capture_rate)capture_rate=rate;
    if(rate!=capture_rate)return; // A capture never concatenates different sample rates.
    const auto limit=std::min<std::size_t>(capture_samples.size(),capture_rate*2*2);
    if(capture_size>=limit)return;
    count=std::min(count,limit-capture_size);
    capture_stamps[capture_stamp_count++]={elapsed,capture_size,count,queued_before};
    std::memcpy(capture_samples.data()+capture_size,pcm,count*sizeof(*pcm));capture_size+=count;
}
void export_capture() {
    if(capture_path.empty() || !capture_size)return;
    std::ofstream file(capture_path,std::ios::binary);
    const auto word=[&](std::uint32_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)file.put(static_cast<char>(value>>(i*8)));};
    file.write("RIFF",4);word(static_cast<std::uint32_t>(36+capture_size*2),4);file.write("WAVEfmt ",8);word(16,4);
    word(1,2);word(2,2);word(capture_rate,4);word(capture_rate*4,4);word(4,2);word(16,2);
    file.write("data",4);word(static_cast<std::uint32_t>(capture_size*2),4);
    file.write(reinterpret_cast<const char*>(capture_samples.data()),static_cast<std::streamsize>(capture_size*2));
    std::ofstream meta(capture_path+".timing.json");meta<<"{\"rate_hz\":"<<capture_rate<<",\"samples\":"<<capture_size<<",\"buffers\":[";
    for(std::size_t i=0;i<capture_stamp_count;++i){const auto& b=capture_stamps[i];if(i)meta<<',';meta<<"{\"elapsed_ns\":"<<b.elapsed_ns<<",\"sample_offset\":"<<b.offset<<",\"sample_count\":"<<b.samples<<",\"sdl_frames_before\":"<<b.queued_before<<'}';}
    meta<<"],\"private\":true,\"position_note\":\"SDL queue is not hardware playback position\"}\n";
}
}
