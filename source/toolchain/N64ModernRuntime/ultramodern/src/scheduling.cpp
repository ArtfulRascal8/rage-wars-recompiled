#include "ultramodern/ultramodern.hpp"
#include <cstdio>
#include <cstdlib>
#include <sstream>

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
static bool rw019_native_trace_enabled() {
    const char* value = std::getenv("XR64_RW019_NATIVE_TRACE");
    return value != nullptr && *value != '\0';
}
#endif

static ultramodern::pause_handler_t pause_handler = nullptr;

void ultramodern::set_pause_handler(ultramodern::pause_handler_t handler) {
    pause_handler = handler;
}

void ultramodern::schedule_running_thread(RDRAM_ARG PTR(OSThread) t_) {
    debug_printf("[Scheduling] Adding thread %d to the running queue\n", TO_PTR(OSThread, t_)->id);
    thread_queue_insert(PASS_RDRAM running_queue, t_);
    TO_PTR(OSThread, t_)->state = OSThreadState::QUEUED;
    {
        std::ostringstream detail;
        detail << "stage=thread-runnable thread=0x" << std::uppercase << std::hex
               << static_cast<unsigned>(t_) << " id=" << std::dec
               << TO_PTR(OSThread, t_)->id << " priority=" << TO_PTR(OSThread, t_)->priority;
        ultramodern::runtime_trace(detail.str());
    }
}

void swap_to_thread(RDRAM_ARG OSThread *to) {
    debug_printf("[Scheduling] Thread %d giving execution to thread %d\n", TO_PTR(OSThread, ultramodern::this_thread())->id, to->id);
    ultramodern::runtime_trace(
            "stage=scheduler-handoff current=0x" +
            std::to_string(static_cast<unsigned>(ultramodern::this_thread())) +
            " target=0x" + std::to_string(static_cast<unsigned>(reinterpret_cast<uintptr_t>(to))));
    // Insert this thread in the running queue.
    ultramodern::thread_queue_insert(PASS_RDRAM ultramodern::running_queue, ultramodern::this_thread());
    TO_PTR(OSThread, ultramodern::this_thread())->state = OSThreadState::QUEUED;
    // Unpause the target thread and wait for this one to be unpaused.
    ultramodern::resume_thread_and_wait(PASS_RDRAM to);
}

void ultramodern::check_running_queue(RDRAM_ARG1) {
    // Check if there are any threads in the running queue.
    if (!thread_queue_empty(PASS_RDRAM running_queue)) {
        // Check if the highest priority thread in the queue is higher priority than the current thread.
        OSThread* next_thread = TO_PTR(OSThread, ultramodern::thread_queue_peek(PASS_RDRAM running_queue));
        OSThread* self = TO_PTR(OSThread, ultramodern::this_thread());
        {
            std::ostringstream detail;
            detail << "stage=scheduler-check current=0x" << std::uppercase << std::hex
                   << static_cast<unsigned>(ultramodern::this_thread())
                   << " next=0x" << static_cast<unsigned>(reinterpret_cast<uintptr_t>(next_thread))
                   << " current_priority=" << std::dec << self->priority
                   << " next_priority=" << next_thread->priority
                   << " decision=" << (next_thread->priority > self->priority ? "switch" : "keep");
            ultramodern::runtime_trace(detail.str());
        }
        if (next_thread->priority > self->priority) {
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
            const auto self_ptr = ultramodern::this_thread();
            const auto selected_ptr = ultramodern::thread_queue_peek(PASS_RDRAM running_queue);
            if (rw019_native_trace_enabled()) {
                std::fprintf(stderr,
                        "RW019_SELECT outgoing=0x%08X id=%d state=%u pri=%d "
                        "selected=0x%08X id=%d state=%u pri=%d reason=higher_priority\n",
                        static_cast<unsigned>(self_ptr), self->id, self->state, self->priority,
                        static_cast<unsigned>(selected_ptr), next_thread->id, next_thread->state, next_thread->priority);
                std::fflush(stderr);
            }
#endif
            ultramodern::thread_queue_pop(PASS_RDRAM running_queue);
            // Swap to the higher priority thread.
            swap_to_thread(PASS_RDRAM next_thread);
        }
    }
}

extern "C" void pause_self(RDRAM_ARG1) {
    if (pause_handler != nullptr) {
        pause_handler(rdram);
        return;
    }
    while (true) {
        // Wait until an external message arrives, then allow the next thread to run.
        ultramodern::wait_for_external_message(PASS_RDRAM1);
        ultramodern::check_running_queue(PASS_RDRAM1);
    }
}

extern "C" void yield_self(RDRAM_ARG1) {
    ultramodern::wait_for_external_message(PASS_RDRAM1);
    ultramodern::check_running_queue(PASS_RDRAM1);
}

extern "C" void yield_self_1ms(RDRAM_ARG1) {
    ultramodern::wait_for_external_message_timed(PASS_RDRAM1, 1);
    ultramodern::check_running_queue(PASS_RDRAM1);
}
