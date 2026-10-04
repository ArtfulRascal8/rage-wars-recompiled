#include <cstdio>
#include <thread>
#include <cassert>
#include <string>
#include <mutex>
#include <unordered_map>
#include <sstream>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "blockingconcurrentqueue.h"

#include "ultramodern/threads.hpp"

// Native APIs only used to set thread names for easier debugging
#ifdef _WIN32
#include <Windows.h>
#endif

static ultramodern::threads::callbacks_t threads_callbacks;
static ultramodern::contextless_handoff_policy_t contextless_handoff_policy = nullptr;

void ultramodern::set_contextless_handoff_policy(contextless_handoff_policy_t policy) {
    contextless_handoff_policy = policy;
}

void ultramodern::threads::set_callbacks(const callbacks_t& callbacks) {
    threads_callbacks = callbacks;
}

std::string ultramodern::threads::get_game_thread_name(const OSThread* t) {
    if (threads_callbacks.get_game_thread_name == nullptr) {
        return "Game Thread " + std::to_string(t->id);
    }
    return threads_callbacks.get_game_thread_name(t);
}

extern "C" void bootproc();

thread_local bool is_entrypoint_thread = false;
// Whether this thread is part of the game (i.e. the start thread or one spawned by osCreateThread)
thread_local bool is_game_thread = false;
thread_local PTR(OSThread) thread_self = NULLPTR;
// Scheduler state is host-owned. It must never live in the guest's OSThread
// object: Rage Wars is free to initialize and reuse that guest memory.
thread_local UltraThreadContext* active_thread_context = nullptr;
static std::mutex thread_contexts_mutex;
static std::unordered_map<OSThread*, UltraThreadContext*> thread_contexts;

static UltraThreadContext* get_thread_context(OSThread* thread) {
    std::lock_guard lock{thread_contexts_mutex};
    const auto found = thread_contexts.find(thread);
    return found != thread_contexts.end() ? found->second : nullptr;
}

static void register_thread_context(OSThread* thread, UltraThreadContext* context) {
    std::lock_guard lock{thread_contexts_mutex};
    thread_contexts.insert_or_assign(thread, context);
}

static UltraThreadContext* unregister_thread_context(OSThread* thread) {
    std::lock_guard lock{thread_contexts_mutex};
    const auto found = thread_contexts.find(thread);
    if (found == thread_contexts.end()) {
        return nullptr;
    }
    UltraThreadContext* context = found->second;
    thread_contexts.erase(found);
    return context;
}

static bool is_registered_context(UltraThreadContext* context) {
    std::lock_guard lock{thread_contexts_mutex};
    for (const auto& [thread, registered] : thread_contexts) {
        (void)thread;
        if (registered == context) {
            return true;
        }
    }
    return false;
}

static UltraThreadContext* current_thread_context(uint8_t* rdram) {
    return active_thread_context != nullptr
            ? active_thread_context
            : get_thread_context(TO_PTR(OSThread, thread_self));
}

void ultramodern::set_entrypoint_thread() {
    ::is_game_thread = true;
    ::is_entrypoint_thread = true;
}

bool ultramodern::is_entrypoint_thread() {
    return ::is_entrypoint_thread;
}

bool ultramodern::is_game_thread() {
    return ::is_game_thread;
}

#if 0
int main(int argc, char** argv) {
    ultramodern::set_entrypoint_thread();

    bootproc();
}
#endif

#if 1
void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t sp, uint64_t arg);
#else
#define run_thread_function(func, sp, arg) func(arg)
#endif

#if defined(_WIN32)
void ultramodern::set_native_thread_name(const std::string& name) {
    std::wstring wname{name.begin(), name.end()};

    HRESULT r;
    r = SetThreadDescription(
        GetCurrentThread(),
        wname.c_str()
    );
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {
    int nPriority = THREAD_PRIORITY_NORMAL;

    // Convert ThreadPriority to Win32 priority
    switch (pri) {
        case ThreadPriority::Low:
            nPriority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case ThreadPriority::Normal:
            nPriority = THREAD_PRIORITY_NORMAL;
            break;
        case ThreadPriority::High:
            nPriority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case ThreadPriority::VeryHigh:
            nPriority = THREAD_PRIORITY_HIGHEST;
            break;
        case ThreadPriority::Critical:
            nPriority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
        default:
            throw std::runtime_error("Invalid thread priority!");
            break;
    }
    // SetThreadPriority(GetCurrentThread(), nPriority);
}
#elif defined(__linux__)
#include <sys/prctl.h>

void ultramodern::set_native_thread_name(const std::string& name) {
    if (name.length() > 15) {
        // Linux only accepts up to 16 characters including the null terminator for a thread name.
        debug_printf("[Thread] The thread name '%s' will be truncated to 15 characters", name.c_str());
    }

    prctl(PR_SET_NAME, name.c_str());
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {
    // TODO linux thread priority
    // printf("set_native_thread_priority unimplemented\n");
    // int nPriority = THREAD_PRIORITY_NORMAL;

    // // Convert ThreadPriority to Win32 priority
    // switch (pri) {
    //     case ThreadPriority::Low:
    //         nPriority = THREAD_PRIORITY_BELOW_NORMAL;
    //         break;
    //     case ThreadPriority::Normal:
    //         nPriority = THREAD_PRIORITY_NORMAL;
    //         break;
    //     case ThreadPriority::High:
    //         nPriority = THREAD_PRIORITY_ABOVE_NORMAL;
    //         break;
    //     case ThreadPriority::VeryHigh:
    //         nPriority = THREAD_PRIORITY_HIGHEST;
    //         break;
    //     case ThreadPriority::Critical:
    //         nPriority = THREAD_PRIORITY_TIME_CRITICAL;
    //         break;
    //     default:
    //         throw std::runtime_error("Invalid thread priority!");
    //         break;
    // }
}
#elif defined(__APPLE__)
void ultramodern::set_native_thread_name(const std::string& name) {
    if (name.length() > 15) {
        // Macs seem to only accept up to 16 characters including the null terminator for a thread name.
        debug_printf("[Thread] The thread name '%s' will be truncated to 15 characters", name.c_str());
    }

    pthread_setname_np(name.c_str());
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {}
#endif

void wait_for_resumed(RDRAM_ARG UltraThreadContext* thread_context) {
    thread_context->running.wait();
    // If this thread's context was replaced by another thread or deleted, destroy it again from its own context.
    // This will trigger thread cleanup instead.
    if (!is_registered_context(thread_context)) {
        osDestroyThread(PASS_RDRAM NULLPTR);
    }
    {
        std::ostringstream detail;
        detail << "stage=thread-awakened " << std::uppercase << std::hex
               << "thread=0x" << static_cast<unsigned>(thread_self)
               << " id=" << std::dec << TO_PTR(OSThread, thread_self)->id;
        ultramodern::runtime_trace(detail.str());
    }
    if (threads_callbacks.thread_activated != nullptr) {
        threads_callbacks.thread_activated(rdram, thread_self);
    }
}

void resume_thread(OSThread* t) {
    debug_printf("[Thread] Resuming execution of thread %d\n", t->id);
    ultramodern::runtime_trace(
            "stage=thread-signal thread=0x" +
            std::to_string(static_cast<unsigned>(reinterpret_cast<uintptr_t>(t))) +
            " id=" + std::to_string(t->id));
    if (UltraThreadContext* context = get_thread_context(t); context != nullptr) {
        context->running.signal();
    }
}

void run_next_thread(RDRAM_ARG1) {
    if (ultramodern::thread_queue_empty(PASS_RDRAM ultramodern::running_queue)) {
        throw std::runtime_error("No threads left to run!\n");
    }

    OSThread* to_run = TO_PTR(OSThread, ultramodern::thread_queue_pop(PASS_RDRAM ultramodern::running_queue));
    debug_printf("[Scheduling] Resuming execution of thread %d\n", to_run->id);
    ultramodern::runtime_trace(
            "stage=scheduler-select thread=0x" +
            std::to_string(static_cast<unsigned>(reinterpret_cast<uintptr_t>(to_run))) +
            " id=" + std::to_string(to_run->id) +
            " priority=" + std::to_string(to_run->priority));
    if (UltraThreadContext* context = get_thread_context(to_run); context != nullptr) {
        context->running.signal();
    }
}

void ultramodern::run_next_thread_and_wait(RDRAM_ARG1) {
    const auto diagnostic_next_ptr = ultramodern::thread_queue_peek(PASS_RDRAM ultramodern::running_queue);
    const auto diagnostic_self = TO_PTR(OSThread, thread_self);
    const auto diagnostic_next = TO_PTR(OSThread, diagnostic_next_ptr);

    ultramodern::runtime_trace(
            "stage=scheduler-run-next current=0x" +
            std::to_string(static_cast<unsigned>(reinterpret_cast<uintptr_t>(diagnostic_self))) +
            " next=0x" + std::to_string(static_cast<unsigned>(diagnostic_next_ptr)));
    UltraThreadContext* cur_context = current_thread_context(rdram);
    if (cur_context == nullptr) {
        if (contextless_handoff_policy == nullptr ||
                !contextless_handoff_policy(diagnostic_self, diagnostic_next)) {
            throw std::runtime_error("Contextless scheduler handoff rejected");
        }
        std::fprintf(stderr, "RW038_CONTEXTLESS_HANDOFF current=0x%08X target=0x%08X mode=run_next\n",
                static_cast<unsigned>(thread_self), static_cast<unsigned>(diagnostic_next_ptr));
        std::fflush(stderr);
        run_next_thread(PASS_RDRAM1);
        return;
    }
    run_next_thread(PASS_RDRAM1);
    wait_for_resumed(PASS_RDRAM cur_context);
}

void ultramodern::resume_thread_and_wait(RDRAM_ARG OSThread *t) {
    const auto next_ptr = ultramodern::thread_queue_peek(PASS_RDRAM ultramodern::running_queue);
    const auto self = TO_PTR(OSThread, thread_self);
    const auto next = TO_PTR(OSThread, next_ptr);

    ultramodern::runtime_trace(
            "stage=scheduler-resume current=0x" +
            std::to_string(static_cast<unsigned>(reinterpret_cast<uintptr_t>(self))) +
            " target=0x" + std::to_string(static_cast<unsigned>(reinterpret_cast<uintptr_t>(t))));
    UltraThreadContext* cur_context = current_thread_context(rdram);
    if (cur_context == nullptr) {
        if (contextless_handoff_policy == nullptr ||
                !contextless_handoff_policy(self, t)) {
            throw std::runtime_error("Contextless scheduler handoff rejected");
        }
        std::fprintf(stderr, "RW038_CONTEXTLESS_HANDOFF current=0x%08X target=0x%08X mode=resume\n",
                static_cast<unsigned>(thread_self),
                static_cast<unsigned>(next_ptr));
        std::fflush(stderr);
        resume_thread(t);
        return;
    }
    resume_thread(t);
    wait_for_resumed(PASS_RDRAM cur_context);
}

static void _thread_func(RDRAM_ARG PTR(OSThread) self_, PTR(thread_func_t) entrypoint, PTR(void) arg, UltraThreadContext* thread_context) {
    OSThread *self = TO_PTR(OSThread, self_);
    debug_printf("[Thread] Thread created: %d\n", self->id);
    thread_self = self_;
    ultramodern::runtime_trace(
            "stage=thread-host-start thread=0x" +
            std::to_string(static_cast<unsigned>(self_)) +
            " id=" + std::to_string(self->id) +
            " entry=0x" + std::to_string(static_cast<unsigned>(entrypoint)));
    active_thread_context = thread_context;
    is_game_thread = true;

    // Set the thread name
    ultramodern::set_native_thread_name(ultramodern::threads::get_game_thread_name(self));
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::High);

    // Signal the initialized semaphore to indicate that this thread can be started.
    thread_context->initialized.signal();

    debug_printf("[Thread] Thread waiting to be started: %d\n", self->id);

    // Wait until the thread is marked as running.
    try {
        wait_for_resumed(PASS_RDRAM thread_context);
    } catch (ultramodern::thread_terminated& terminated) {
    }

    // Make sure the thread wasn't replaced or destroyed before it was started.
    if (is_registered_context(thread_context)) {
        debug_printf("[Thread] Thread started: %d\n", self->id);
        try {
            // Run the thread's function with the provided argument.
            ultramodern::runtime_trace(
                    "stage=guest-entry thread=0x" +
                    std::to_string(static_cast<unsigned>(self_)) +
                    " id=" + std::to_string(self->id) +
                    " entry=0x" + std::to_string(static_cast<unsigned>(entrypoint)));
            run_thread_function(PASS_RDRAM entrypoint, self->sp, arg);
        } catch (ultramodern::thread_terminated& terminated) {
        }
    }
    else {
        debug_printf("[Thread] Thread destroyed before being started: %d\n", self->id);
    }

    // Check if the thread hasn't been destroyed or replaced. If so, then the thread terminated or destroyed itself,
    // so mark this thread as destroyed and run the next queued thread.
    if (is_registered_context(thread_context)) {
        unregister_thread_context(self);
        run_next_thread(PASS_RDRAM1);
    }

    // Dispose of this thread now that it's completed or terminated.
    ultramodern::cleanup_thread(thread_context);
}

extern "C" void osStartThread(RDRAM_ARG PTR(OSThread) t_) {
    OSThread* t = TO_PTR(OSThread, t_);
    debug_printf("[os] Start Thread %d\n", t->id);

    // If this is a game thread, insert the new thread into the running queue and then check the running queue.
    if (thread_self) {
        ultramodern::schedule_running_thread(PASS_RDRAM t_);
        ultramodern::check_running_queue(PASS_RDRAM1);
    }
    // Otherwise, immediately start the thread and terminate this one.
    else {
        t->state = OSThreadState::QUEUED;
        resume_thread(t);
        //throw ultramodern::thread_terminated{};
    }
}

extern "C" void osCreateThread(RDRAM_ARG PTR(OSThread) t_, OSId id, PTR(thread_func_t) entrypoint, PTR(void) arg, PTR(void) sp, OSPri pri) {
    debug_printf("[os] Create Thread %d\n", id);
    OSThread *t = TO_PTR(OSThread, t_);
    
    t->next = NULLPTR;
    t->queue = NULLPTR;
    t->priority = pri;
    t->id = id;
    t->state = OSThreadState::STOPPED;
    t->sp = sp - 0x10; // Set up the first stack frame
    ultramodern::runtime_trace(
            "stage=thread-created thread=0x" +
            std::to_string(static_cast<unsigned>(t_)) +
            " id=" + std::to_string(id) +
            " priority=" + std::to_string(pri) +
            " entry=0x" + std::to_string(static_cast<unsigned>(entrypoint)));
    ultramodern::thread_queue_register_thread(t_, pri);

    // Spawn a new thread, which will immediately pause itself and wait until it's been started.
    // Pass the context as an argument to the thread function to ensure that it can't get cleared before the thread captures its value.
    UltraThreadContext* context = new UltraThreadContext{};
    register_thread_context(t, context);
    context->host_thread = std::thread{_thread_func, PASS_RDRAM t_, entrypoint, arg, context};

    // Wait until the thread is initialized to indicate that it's ready to be started.
    context->initialized.wait();
    debug_printf("[os] Thread %d is ready to be started\n", t->id);
}

extern "C" void osStopThread(RDRAM_ARG PTR(OSThread) t_) {
    if (t_ == NULLPTR) {
        t_ = thread_self;
    }
    // Check if the thread is stopping itself (arg is null or thread_self).
    if (t_ == thread_self) {
        ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
    }
    else {
        assert(false);
    }
}

extern "C" void osDestroyThread(RDRAM_ARG PTR(OSThread) t_) {
    if (t_ == NULLPTR) {
        t_ = thread_self;
    }
    OSThread* t = TO_PTR(OSThread, t_);
    // Check if the thread is destroying itself (arg is null or thread_self)
    if (t_ == thread_self) {
        throw ultramodern::thread_terminated{};
    }
    // Unregister by the native guest-thread handle. RageWarsScheduler owns queue
    // membership, including message wait queues; OSThread mirrors must not be
    // consulted to rediscover the queue during destruction.
    UltraThreadContext* cur_context = unregister_thread_context(t);
    ultramodern::thread_queue_unregister_thread(PASS_RDRAM t_);
    if (cur_context != nullptr) {
        // Mark the target thread as destroyed and resume it. When it starts it'll check this and terminate itself instead of resuming.
        cur_context->running.signal();
    }
}

extern "C" void osSetThreadPri(RDRAM_ARG PTR(OSThread) t_, OSPri pri) {
    if (t_ == NULLPTR) {
        t_ = thread_self;
    }
    OSThread* t = TO_PTR(OSThread, t_);

    if (t->priority != pri) {
        const auto queue = ultramodern::thread_queue_for_thread(t_);
        t->priority = pri;

        if (t_ != ultramodern::this_thread() &&
                t->state != OSThreadState::STOPPED && queue != NULLPTR) {
            ultramodern::thread_queue_set_priority(PASS_RDRAM t_, pri);
        }

        ultramodern::check_running_queue(PASS_RDRAM1);
    }
}

extern "C" OSPri osGetThreadPri(RDRAM_ARG PTR(OSThread) t) {
    if (t == NULLPTR) {
        t = thread_self;
    }
    return TO_PTR(OSThread, t)->priority;
}

extern "C" OSId osGetThreadId(RDRAM_ARG PTR(OSThread) t) {
    if (t == NULLPTR) {
        t = thread_self;
    }
    return TO_PTR(OSThread, t)->id;
}

PTR(OSThread) ultramodern::this_thread() {
    return thread_self;
}

static std::thread thread_cleaner_thread;
static moodycamel::BlockingConcurrentQueue<UltraThreadContext*> deleted_threads{};
extern std::atomic_bool exited;

void thread_cleaner_func() {
    using namespace std::chrono_literals;
    while (!exited) {
        UltraThreadContext* to_delete;
        if (deleted_threads.wait_dequeue_timed(to_delete, 10ms)) {
            debug_printf("[Cleanup] Deleting thread context %p\n", to_delete);

            to_delete->host_thread.join();
            delete to_delete;
        }
    }
}

void ultramodern::init_thread_cleanup() {
    thread_cleaner_thread = std::thread{thread_cleaner_func};
}

void ultramodern::cleanup_thread(UltraThreadContext *cur_context) {
    deleted_threads.enqueue(cur_context);
}

void ultramodern::join_thread_cleaner_thread() {
    thread_cleaner_thread.join();
}
