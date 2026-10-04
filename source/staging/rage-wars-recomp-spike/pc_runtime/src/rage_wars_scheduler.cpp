#include "rage_wars_scheduler.hpp"

#include <algorithm>

namespace rage_wars {
namespace {

RageWarsScheduler g_scheduler;

} // namespace

void RageWarsScheduler::register_thread(
        GuestThreadHandle thread, std::int32_t priority) {
    if (thread == 0) {
        return;
    }
    std::lock_guard lock{mutex_};
    NativeThread& native_thread = threads_[thread];
    native_thread.priority = priority;
}

RageWarsScheduler::QueueMutation RageWarsScheduler::unregister_thread(
        GuestThreadHandle thread) {
    std::lock_guard lock{mutex_};
    const auto found = threads_.find(thread);
    if (found == threads_.end()) {
        return {};
    }
    const QueueId queue = found->second.queue;
    if (queue != 0) {
        erase_from_queue_locked(queue, thread);
    }
    threads_.erase(found);
    return {.previous_queue = queue, .queue = 0};
}

RageWarsScheduler::QueueMutation RageWarsScheduler::set_thread_priority(
        GuestThreadHandle thread, std::int32_t priority) {
    std::lock_guard lock{mutex_};
    NativeThread& native_thread = threads_[thread];
    const QueueId queue = native_thread.queue;
    native_thread.priority = priority;
    if (queue == 0) {
        return {.previous_queue = 0, .queue = 0};
    }
    erase_from_queue_locked(queue, thread);
    auto& threads = queues_[queue].threads;
    const auto position = std::find_if(
            threads.begin(), threads.end(),
            [this, priority](GuestThreadHandle candidate) {
                return threads_.at(candidate).priority <= priority;
            });
    threads.insert(position, thread);
    return {.previous_queue = queue, .queue = queue};
}

std::vector<RageWarsScheduler::GuestThreadHandle>
RageWarsScheduler::reset_queue(QueueId queue) {
    std::lock_guard lock{mutex_};
    std::vector<GuestThreadHandle> removed;
    const auto found = queues_.find(queue);
    if (found == queues_.end()) {
        return removed;
    }
    removed.assign(found->second.threads.begin(), found->second.threads.end());
    for (const GuestThreadHandle thread : removed) {
        const auto member = threads_.find(thread);
        if (member != threads_.end() && member->second.queue == queue) {
            member->second.queue = 0;
        }
    }
    queues_.erase(found);
    return removed;
}

RageWarsScheduler::QueueMutation RageWarsScheduler::enqueue(
        QueueId queue, GuestThreadHandle thread) {
    if (thread == 0) {
        return {.queue = queue};
    }
    std::lock_guard lock{mutex_};
    NativeThread& native_thread = threads_[thread];
    const QueueId previous_queue = native_thread.queue;
    if (previous_queue != 0) {
        erase_from_queue_locked(previous_queue, thread);
    }

    auto& threads = queues_[queue].threads;
    const auto position = std::find_if(
            threads.begin(), threads.end(),
            [this, priority = native_thread.priority](GuestThreadHandle candidate) {
                return threads_.at(candidate).priority <= priority;
            });
    threads.insert(position, thread);
    native_thread.queue = queue;
    return {.previous_queue = previous_queue, .queue = queue};
}

RageWarsScheduler::PopResult RageWarsScheduler::dequeue(QueueId queue) {
    std::lock_guard lock{mutex_};
    const auto found = queues_.find(queue);
    if (found == queues_.end() || found->second.threads.empty()) {
        return {.queue = queue};
    }
    const GuestThreadHandle thread = found->second.threads.front();
    found->second.threads.pop_front();
    if (const auto member = threads_.find(thread); member != threads_.end()) {
        member->second.queue = 0;
    }
    return {.thread = thread, .queue = queue};
}

bool RageWarsScheduler::remove(QueueId queue, GuestThreadHandle thread) {
    std::lock_guard lock{mutex_};
    const auto member = threads_.find(thread);
    if (member == threads_.end() || member->second.queue != queue) {
        return false;
    }
    erase_from_queue_locked(queue, thread);
    member->second.queue = 0;
    return true;
}

bool RageWarsScheduler::empty(QueueId queue) const {
    std::lock_guard lock{mutex_};
    const auto found = queues_.find(queue);
    return found == queues_.end() || found->second.threads.empty();
}

RageWarsScheduler::GuestThreadHandle RageWarsScheduler::peek(QueueId queue) const {
    std::lock_guard lock{mutex_};
    const auto found = queues_.find(queue);
    return found == queues_.end() || found->second.threads.empty()
            ? 0
            : found->second.threads.front();
}

RageWarsScheduler::QueueId RageWarsScheduler::queue_for_thread(
        GuestThreadHandle thread) const {
    std::lock_guard lock{mutex_};
    const auto found = threads_.find(thread);
    return found == threads_.end() ? 0 : found->second.queue;
}

std::vector<RageWarsScheduler::GuestThreadHandle>
RageWarsScheduler::snapshot(QueueId queue) const {
    std::lock_guard lock{mutex_};
    const auto found = queues_.find(queue);
    if (found == queues_.end()) {
        return {};
    }
    return {found->second.threads.begin(), found->second.threads.end()};
}

void RageWarsScheduler::erase_from_queue_locked(
        QueueId queue, GuestThreadHandle thread) {
    const auto found = queues_.find(queue);
    if (found == queues_.end()) {
        return;
    }
    auto& threads = found->second.threads;
    threads.erase(std::remove(threads.begin(), threads.end(), thread), threads.end());
}

bool RageWarsScheduler::permits_contextless_handoff(
        std::uint32_t current_thread, std::uint32_t target_thread) const {
    // A root/native scheduler has a guest thread identity but no suspended
    // host continuation. It may only hand off between concrete guest threads.
    return current_thread != 0 && target_thread != 0;
}

RageWarsScheduler &scheduler() {
    return g_scheduler;
}

} // namespace rage_wars
