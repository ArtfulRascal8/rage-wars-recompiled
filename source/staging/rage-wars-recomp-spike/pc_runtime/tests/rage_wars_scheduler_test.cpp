#include "rage_wars_scheduler.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "RageWarsScheduler contract failure: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    rage_wars::RageWarsScheduler service;
    constexpr auto runnable_queue = rage_wars::RageWarsScheduler::running_queue_id;
    constexpr rage_wars::RageWarsScheduler::QueueId receive_wait_queue = 0x800F'0001ULL;
    constexpr rage_wars::RageWarsScheduler::QueueId send_wait_queue = 0x800F'0002ULL;
    constexpr auto runnable_thread = 0x800F'1000U;
    constexpr auto receive_waiter = 0x800F'2000U;
    constexpr auto send_waiter = 0x800F'3000U;

    service.register_thread(runnable_thread, 100);
    service.register_thread(receive_waiter, 90);
    service.register_thread(send_waiter, 80);
    (void)service.enqueue(runnable_queue, runnable_thread);
    (void)service.enqueue(receive_wait_queue, receive_waiter);
    (void)service.enqueue(send_wait_queue, send_waiter);

    require(service.queue_for_thread(receive_waiter) == receive_wait_queue,
            "receive waiter lost its native queue identity");
    require(service.queue_for_thread(send_waiter) == send_wait_queue,
            "send waiter lost its native queue identity");

    const auto receive_destroy = service.unregister_thread(receive_waiter);
    const auto send_destroy = service.unregister_thread(send_waiter);
    require(receive_destroy.previous_queue == receive_wait_queue,
            "destroy did not identify the native receive wait queue");
    require(send_destroy.previous_queue == send_wait_queue,
            "destroy did not identify the native send wait queue");
    require(service.empty(receive_wait_queue),
            "destroyed receive waiter remained in the native queue");
    require(service.empty(send_wait_queue),
            "destroyed send waiter remained in the native queue");
    require(service.peek(runnable_queue) == runnable_thread,
            "destroying waiters corrupted the runnable queue");

    require(service.permits_contextless_handoff(0x800FD2F0U, 0x801197C0U),
            "root scheduler could not hand off to a concrete guest worker");
    require(!service.permits_contextless_handoff(0, 0x801197C0U),
            "missing current guest thread was accepted");
    require(!service.permits_contextless_handoff(0x800FD2F0U, 0),
            "missing target guest thread was accepted");
    std::cout << "RageWarsScheduler contract: PASS\n";
    return EXIT_SUCCESS;
}
