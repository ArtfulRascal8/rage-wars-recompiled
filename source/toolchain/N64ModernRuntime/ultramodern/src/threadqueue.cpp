#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <rage_wars_scheduler.hpp>
#include <ultramodern/ultramodern.hpp>

namespace {
using GuestThreadHandle = PTR(OSThread);
using GuestQueueHandle = PTR(PTR(OSThread));
using QueueId = rage_wars::RageWarsScheduler::QueueId;

std::mutex mirror_mutex;
std::unordered_map<QueueId, GuestQueueHandle> queue_mirror_slots;

template <typename T>
T* guest_rdram_ptr(uint8_t* rdram, PTR(T) guest_address) {
    return reinterpret_cast<T*>(
            rdram + (static_cast<std::uint32_t>(guest_address) & 0x1FFFFFFFU));
}

QueueId queue_id(GuestQueueHandle queue) {
    return queue == ultramodern::running_queue
            ? rage_wars::RageWarsScheduler::running_queue_id
            : static_cast<std::uint32_t>(queue);
}

QueueId wait_queue_id(PTR(OSMesgQueue) mq, ultramodern::ThreadWaitQueue kind) {
    constexpr QueueId wait_queue_tag = QueueId{1} << 63U;
    return wait_queue_tag |
            (static_cast<QueueId>(static_cast<std::uint32_t>(mq) & 0x1FFFFFFFU) << 2U) |
            static_cast<QueueId>(kind == ultramodern::ThreadWaitQueue::Send);
}

GuestQueueHandle wait_mirror_slot(
        PTR(OSMesgQueue) mq, ultramodern::ThreadWaitQueue kind) {
    const std::uint32_t offset = kind == ultramodern::ThreadWaitQueue::Receive
            ? offsetof(OSMesgQueue, blocked_on_recv)
            : offsetof(OSMesgQueue, blocked_on_send);
    return static_cast<GuestQueueHandle>(static_cast<std::uint32_t>(mq) + offset);
}

void register_mirror(QueueId id, GuestQueueHandle slot) {
    if (id == rage_wars::RageWarsScheduler::running_queue_id) {
        return;
    }
    std::lock_guard lock{mirror_mutex};
    queue_mirror_slots[id] = slot;
}

GuestQueueHandle mirror_slot(QueueId id) {
    if (id == rage_wars::RageWarsScheduler::running_queue_id) {
        return ultramodern::running_queue;
    }
    std::lock_guard lock{mirror_mutex};
    const auto found = queue_mirror_slots.find(id);
    return found == queue_mirror_slots.end() ? NULLPTR : found->second;
}

OSThread* guest_thread(uint8_t* rdram, GuestThreadHandle thread) {
    return guest_rdram_ptr<OSThread>(rdram, thread);
}

void clear_thread_mirror(uint8_t* rdram, GuestThreadHandle thread) {
    if (thread == NULLPTR) {
        return;
    }
    OSThread* guest = guest_thread(rdram, thread);
    guest->next = NULLPTR;
    guest->queue = NULLPTR;
}

void mirror_queue(uint8_t* rdram, QueueId id) {
    const GuestQueueHandle slot = mirror_slot(id);
    const auto threads = rage_wars::scheduler().snapshot(id);
    for (std::size_t index = 0; index < threads.size(); ++index) {
        OSThread* guest = guest_thread(rdram, threads[index]);
        guest->next = index + 1 < threads.size() ? threads[index + 1] : NULLPTR;
        guest->queue = slot;
    }
    if (slot != ultramodern::running_queue && slot != NULLPTR) {
        *guest_rdram_ptr<GuestThreadHandle>(rdram, slot) =
                threads.empty() ? NULLPTR : threads.front();
    }
}

void refresh_mutation(uint8_t* rdram, const rage_wars::RageWarsScheduler::QueueMutation& mutation) {
    if (mutation.previous_queue != 0) {
        mirror_queue(rdram, mutation.previous_queue);
    }
    if (mutation.queue != 0 && mutation.queue != mutation.previous_queue) {
        mirror_queue(rdram, mutation.queue);
    }
}

void reset_queue(uint8_t* rdram, QueueId id) {
    for (const GuestThreadHandle thread : rage_wars::scheduler().reset_queue(id)) {
        clear_thread_mirror(rdram, thread);
    }
    mirror_queue(rdram, id);
}

void insert_queue(uint8_t* rdram, QueueId id, GuestThreadHandle thread) {
    refresh_mutation(rdram, rage_wars::scheduler().enqueue(id, thread));
}

GuestThreadHandle pop_queue(uint8_t* rdram, QueueId id) {
    const auto result = rage_wars::scheduler().dequeue(id);
    clear_thread_mirror(rdram, result.thread);
    mirror_queue(rdram, id);
    return result.thread;
}

} // namespace

void ultramodern::thread_queue_register_thread(
        GuestThreadHandle thread, OSPri priority) {
    rage_wars::scheduler().register_thread(thread, priority);
}

void ultramodern::thread_queue_unregister_thread(
        RDRAM_ARG GuestThreadHandle thread) {
    const auto mutation = rage_wars::scheduler().unregister_thread(thread);
    clear_thread_mirror(rdram, thread);
    refresh_mutation(rdram, mutation);
}

void ultramodern::thread_queue_set_priority(
        RDRAM_ARG GuestThreadHandle thread, OSPri priority) {
    refresh_mutation(rdram, rage_wars::scheduler().set_thread_priority(thread, priority));
}

void ultramodern::thread_queue_reset(RDRAM_ARG GuestQueueHandle queue) {
    const QueueId id = queue_id(queue);
    register_mirror(id, queue);
    reset_queue(rdram, id);
}

void ultramodern::thread_queue_insert(
        RDRAM_ARG GuestQueueHandle queue, GuestThreadHandle thread) {
    const QueueId id = queue_id(queue);
    register_mirror(id, queue);
    insert_queue(rdram, id, thread);
}

GuestThreadHandle ultramodern::thread_queue_pop(
        RDRAM_ARG GuestQueueHandle queue) {
    const QueueId id = queue_id(queue);
    register_mirror(id, queue);
    return pop_queue(rdram, id);
}

bool ultramodern::thread_queue_remove(
        RDRAM_ARG GuestQueueHandle queue, GuestThreadHandle thread) {
    const QueueId id = queue_id(queue);
    register_mirror(id, queue);
    const bool removed = rage_wars::scheduler().remove(id, thread);
    if (removed) {
        clear_thread_mirror(rdram, thread);
        mirror_queue(rdram, id);
    }
    return removed;
}

bool ultramodern::thread_queue_empty(RDRAM_ARG GuestQueueHandle queue) {
    return rage_wars::scheduler().empty(queue_id(queue));
}

GuestThreadHandle ultramodern::thread_queue_peek(RDRAM_ARG GuestQueueHandle queue) {
    return rage_wars::scheduler().peek(queue_id(queue));
}

GuestQueueHandle ultramodern::thread_queue_for_thread(GuestThreadHandle thread) {
    return mirror_slot(rage_wars::scheduler().queue_for_thread(thread));
}

void ultramodern::thread_wait_queue_reset(
        RDRAM_ARG PTR(OSMesgQueue) mq, ThreadWaitQueue kind) {
    const QueueId id = wait_queue_id(mq, kind);
    register_mirror(id, wait_mirror_slot(mq, kind));
    reset_queue(rdram, id);
}

void ultramodern::thread_wait_queue_insert(
        RDRAM_ARG PTR(OSMesgQueue) mq, ThreadWaitQueue kind,
        GuestThreadHandle thread) {
    const QueueId id = wait_queue_id(mq, kind);
    register_mirror(id, wait_mirror_slot(mq, kind));
    insert_queue(rdram, id, thread);
}

GuestThreadHandle ultramodern::thread_wait_queue_pop(
        RDRAM_ARG PTR(OSMesgQueue) mq, ThreadWaitQueue kind) {
    const QueueId id = wait_queue_id(mq, kind);
    register_mirror(id, wait_mirror_slot(mq, kind));
    return pop_queue(rdram, id);
}

bool ultramodern::thread_wait_queue_empty(
        RDRAM_ARG PTR(OSMesgQueue) mq, ThreadWaitQueue kind) {
    return rage_wars::scheduler().empty(wait_queue_id(mq, kind));
}
