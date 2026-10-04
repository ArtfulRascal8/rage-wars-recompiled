#pragma once
#include <atomic>
#include <cstdint>

// UI requests are committed only by the guest's world-render pass. Both eye
// replays and the first-person pass therefore use the same display-list choice.
class RageWarsBodyToggle {
public:
    explicit RageWarsBodyToggle(bool initial = false) : enabled_(initial) {}
    void request() { pending_.fetch_add(1, std::memory_order_relaxed); }
    bool commit() {
        if ((pending_.exchange(0, std::memory_order_relaxed) & 1U) == 0) return false;
        enabled_ = !enabled_;
        return true;
    }
    bool enabled() const { return enabled_; } // Guest render thread only.
private:
    std::atomic<std::uint32_t> pending_{0};
    bool enabled_;
};
