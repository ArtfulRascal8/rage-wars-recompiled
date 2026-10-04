#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rage_wars {

class RageWarsScheduler {
public:
    using GuestThreadHandle = std::uint32_t;
    using QueueId = std::uint64_t;

    struct QueueMutation {
        QueueId previous_queue = 0;
        QueueId queue = 0;
    };

    struct PopResult {
        GuestThreadHandle thread = 0;
        QueueId queue = 0;
    };

    // Queue identity is native. A guest queue-head address may be used as an
    // identity token, but never as linked-list storage or traversal authority.
    static constexpr QueueId running_queue_id = ~QueueId{0};

    void register_thread(GuestThreadHandle thread, std::int32_t priority);
    [[nodiscard]] QueueMutation unregister_thread(GuestThreadHandle thread);
    [[nodiscard]] QueueMutation set_thread_priority(
            GuestThreadHandle thread, std::int32_t priority);

    [[nodiscard]] std::vector<GuestThreadHandle> reset_queue(QueueId queue);
    [[nodiscard]] QueueMutation enqueue(QueueId queue, GuestThreadHandle thread);
    [[nodiscard]] PopResult dequeue(QueueId queue);
    [[nodiscard]] bool remove(QueueId queue, GuestThreadHandle thread);
    [[nodiscard]] bool empty(QueueId queue) const;
    [[nodiscard]] GuestThreadHandle peek(QueueId queue) const;
    [[nodiscard]] QueueId queue_for_thread(GuestThreadHandle thread) const;
    [[nodiscard]] std::vector<GuestThreadHandle> snapshot(QueueId queue) const;

    [[nodiscard]] bool permits_contextless_handoff(
            std::uint32_t current_thread, std::uint32_t target_thread) const;

private:
    struct NativeThread {
        std::int32_t priority = 0;
        QueueId queue = 0;
    };

    struct NativeQueue {
        std::deque<GuestThreadHandle> threads;
    };

    void erase_from_queue_locked(QueueId queue, GuestThreadHandle thread);

    mutable std::mutex mutex_;
    std::unordered_map<GuestThreadHandle, NativeThread> threads_;
    std::unordered_map<QueueId, NativeQueue> queues_;
};

RageWarsScheduler &scheduler();
void install_scheduler_service();

} // namespace rage_wars
