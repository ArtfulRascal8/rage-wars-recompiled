#pragma once
#include "n64_decoded_frame.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <cstdint>
namespace xr64 {
struct N64NoPresentationState {};
template<class PresentationState> struct N64FrameSnapshotOf {
    N64DecodedFrame frame;
    std::uint64_t generation = 0;
    double decode_cpu_ms = 0;
    N64RawFast3DTaskStats stats{};
    bool menu_panel=false;
    bool startup=false;
    // Host decode-completion time, not the guest simulation timestamp.
    std::chrono::steady_clock::time_point decoded_at{};
    PresentationState player{};
};
// Publishing transfers ownership. A presentation retains the same const snapshot
// across both eyes even if a newer generation replaces the mailbox entry.
template<class PresentationState> class N64FrameMailboxOf {
    using Snapshot=N64FrameSnapshotOf<PresentationState>;
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> latest_;
public:
    void publish(Snapshot snapshot) {
        auto owned = std::make_shared<const Snapshot>(std::move(snapshot));
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::move(owned);
    }
    std::shared_ptr<const Snapshot> acquire() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_;
    }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_.reset();
    }
};
using N64FrameSnapshot=N64FrameSnapshotOf<N64NoPresentationState>;
using N64FrameMailbox=N64FrameMailboxOf<N64NoPresentationState>;
}
