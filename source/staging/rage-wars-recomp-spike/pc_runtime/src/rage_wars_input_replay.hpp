#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace rage_wars {

struct InputReplayTraceEvent {
    std::uint64_t elapsed_ms = 0;
    std::uint64_t vi = 0;
    std::uint16_t button_mask = 0;
    std::uint16_t active_buttons = 0;
    bool pressed = false;
    std::string button;
};

class RageWarsInputReplay {
public:
    using TraceCallback = std::function<void(const InputReplayTraceEvent&)>;

    bool load_file(const std::string& path, std::string& error);
    void disable();
    void set_trace_callback(TraceCallback callback);

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool armed() const;
    [[nodiscard]] std::size_t event_count() const;

    void arm(std::uint64_t now_ms, std::uint64_t vi);
    std::uint16_t sample(std::uint64_t now_ms, std::uint64_t vi, float* x=nullptr, float* y=nullptr);

private:
    struct Event {
        int axis = -1;
        float axis_value = 0;
        std::uint64_t at_ms = 0;
        std::uint16_t button_mask = 0;
        bool pressed = false;
        std::string button;
    };

    mutable std::mutex mutex_;
    std::vector<Event> events_;
    TraceCallback trace_callback_;
    std::size_t next_event_ = 0;
    std::uint64_t anchor_ms_ = 0;
    std::uint16_t active_buttons_ = 0;
    float axes_[2] = {};
    bool enabled_ = false;
    bool armed_ = false;
};

} // namespace rage_wars
