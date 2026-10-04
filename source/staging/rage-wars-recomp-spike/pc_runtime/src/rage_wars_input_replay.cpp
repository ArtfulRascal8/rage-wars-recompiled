#include "rage_wars_input_replay.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace rage_wars {
namespace {

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::toupper(character));
            });
    return value;
}

bool parse_button(
        const std::string& token, std::uint16_t& mask, std::string& name) {
    const auto normalized = uppercase(token);
    // Explicit diagnostic replays can traverse and exit native control pages.
    struct Button { const char* name; std::uint16_t mask; };
    static constexpr Button buttons[] = {
        {"B",0x4000},{"Z",0x2000},{"UP",0x0800},{"DOWN",0x0400},
        {"LEFT",0x0200},{"RIGHT",0x0100},{"L",0x0020},{"R",0x0010},
        {"C_UP",0x0008},{"C_DOWN",0x0004},{"C_LEFT",0x0002},{"C_RIGHT",0x0001}};
    for (const auto& button : buttons) {
        if (normalized == button.name) { mask=button.mask; name=button.name; return true; }
    }
    if (normalized == "A") {
        mask = 0x8000U;
        name = "A";
        return true;
    }
    if (normalized == "START" || normalized == "START_BUTTON") {
        mask = 0x1000U;
        name = "START";
        return true;
    }
    return false;
}

bool parse_action(const std::string& token, bool& pressed) {
    const auto normalized = uppercase(token);
    if (normalized == "PRESS" || normalized == "DOWN") {
        pressed = true;
        return true;
    }
    if (normalized == "RELEASE" || normalized == "UP") {
        pressed = false;
        return true;
    }
    return false;
}

} // namespace

bool RageWarsInputReplay::load_file(
        const std::string& path, std::string& error) {
    if (path.empty()) {
        disable();
        return true;
    }

    std::ifstream input(path);
    if (!input) {
        error = "input replay file could not be opened";
        return false;
    }

    std::vector<Event> parsed;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }

        std::istringstream fields(line.substr(first));
        Event event;
        std::string button;
        std::string action;
        std::string extra;
        if (!(fields >> event.at_ms >> button >> action) || (fields >> extra)) {
            error = "invalid input replay line " + std::to_string(line_number) +
                    "; expected: <time_ms> <N64_button> <press|release>";
            return false;
        }
        if (button=="STICK_X" || button=="STICK_Y") {
            std::istringstream value(action);
            if (!(value>>event.axis_value) || (value>>extra) || !std::isfinite(event.axis_value) ||
                    std::abs(event.axis_value)>1.0F) {
                error="invalid replay analog axis on line "+std::to_string(line_number);
                return false;
            }
            event.axis=button=="STICK_X"?0:1;
        } else {
        if (!parse_button(button, event.button_mask, event.button)) {
            error = "invalid input replay button on line " +
                    std::to_string(line_number);
            return false;
        }
        if (!parse_action(action, event.pressed)) {
            error = "invalid input replay action on line " +
                    std::to_string(line_number);
            return false;
        }
        }
        parsed.push_back(std::move(event));
    }

    std::stable_sort(parsed.begin(), parsed.end(),
            [](const Event& left, const Event& right) {
                return left.at_ms < right.at_ms;
            });

    std::lock_guard lock(mutex_);
    events_ = std::move(parsed);
    next_event_ = 0;
    anchor_ms_ = 0;
    active_buttons_ = 0;
    axes_[0]=axes_[1]=0;
    enabled_ = true;
    armed_ = false;
    return true;
}

void RageWarsInputReplay::disable() {
    std::lock_guard lock(mutex_);
    events_.clear();
    next_event_ = 0;
    anchor_ms_ = 0;
    active_buttons_ = 0;
    axes_[0]=axes_[1]=0;
    enabled_ = false;
    armed_ = false;
}

void RageWarsInputReplay::set_trace_callback(TraceCallback callback) {
    std::lock_guard lock(mutex_);
    trace_callback_ = std::move(callback);
}

bool RageWarsInputReplay::enabled() const {
    std::lock_guard lock(mutex_);
    return enabled_;
}

bool RageWarsInputReplay::armed() const {
    std::lock_guard lock(mutex_);
    return armed_;
}

std::size_t RageWarsInputReplay::event_count() const {
    std::lock_guard lock(mutex_);
    return events_.size();
}

void RageWarsInputReplay::arm(std::uint64_t now_ms, std::uint64_t) {
    std::lock_guard lock(mutex_);
    if (!enabled_ || armed_) {
        return;
    }
    anchor_ms_ = now_ms;
    next_event_ = 0;
    active_buttons_ = 0;
    axes_[0]=axes_[1]=0;
    armed_ = true;
}

std::uint16_t RageWarsInputReplay::sample(
        std::uint64_t now_ms, std::uint64_t vi, float* x, float* y) {
    std::vector<InputReplayTraceEvent> emitted;
    TraceCallback callback;
    std::uint16_t result = 0;

    {
        std::unique_lock lock(mutex_);
        if (x) *x=0;
        if (y) *y=0;
        if (!enabled_ || !armed_) {
            return 0;
        }

        const auto elapsed_ms = now_ms >= anchor_ms_ ? now_ms - anchor_ms_ : 0;
        while (next_event_ < events_.size() &&
                events_[next_event_].at_ms <= elapsed_ms) {
            const auto& event = events_[next_event_++];
            if (event.axis>=0) {
                axes_[event.axis]=event.axis_value;
                continue;
            }
            if (event.pressed) {
                active_buttons_ = static_cast<std::uint16_t>(
                        active_buttons_ | event.button_mask);
            } else {
                active_buttons_ = static_cast<std::uint16_t>(
                        active_buttons_ & static_cast<std::uint16_t>(
                                ~event.button_mask));
            }
            emitted.push_back(InputReplayTraceEvent{
                    elapsed_ms, vi, event.button_mask, active_buttons_,
                    event.pressed, event.button});
        }
        result = active_buttons_;
        if (x) *x=axes_[0];
        if (y) *y=axes_[1];
        callback = trace_callback_;
    }

    if (callback) {
        for (const auto& event : emitted) {
            callback(event);
        }
    }
    return result;
}

} // namespace rage_wars
