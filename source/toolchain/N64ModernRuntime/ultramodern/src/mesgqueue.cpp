#include <bitset>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "blockingconcurrentqueue.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

struct QueuedMessage {
    PTR(OSMesgQueue) mq;
    OSMesg mesg;
    bool jam;
    bool requeue_if_blocked;
    int trace_source;
};

static moodycamel::BlockingConcurrentQueue<QueuedMessage> external_messages {};
std::bitset<32> requeue_enabled;
static ultramodern::vi_message_callback_t* vi_message_callback = nullptr;
static uint8_t* vi_message_rdram = nullptr;

void ultramodern::set_vi_message_callback(vi_message_callback_t* callback, uint8_t* rdram) {
    vi_message_callback = callback;
    vi_message_rdram = rdram;
}

std::string rw073_queue_tag(PTR(OSMesgQueue) mq, OSMesg msg);
std::string rw073_thread_tag(PTR(OSThread) thread);
bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block);
void ultramodern::set_message_queue_control(const ultramodern::MessageQueueControl& mqc) {
    requeue_enabled.reset();
    requeue_enabled.set(static_cast<int>(EventMessageSource::Timer), mqc.requeue_timer);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Sp), mqc.requeue_sp);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Si), mqc.requeue_si);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Ai), mqc.requeue_ai);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Vi), mqc.requeue_vi);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Pi), mqc.requeue_pi);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Dp), mqc.requeue_dp);
}

void ultramodern::enqueue_external_message_src(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, EventMessageSource src) {
    std::ostringstream detail;
    detail << "stage=message-enqueued source=" << static_cast<int>(src) << " "
           << rw073_queue_tag(mq, msg);
    ultramodern::runtime_trace(detail.str());
    if (src == EventMessageSource::Vi && vi_message_callback != nullptr) {
        vi_message_callback(vi_message_rdram, mq, msg);
        return;
    }
    external_messages.enqueue({mq, msg, jam, requeue_enabled[static_cast<int>(src)], static_cast<int>(src)});
}

void ultramodern::enqueue_external_message(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, bool requeue_if_blocked) {
    ultramodern::runtime_trace(
            "stage=message-enqueued source=guest " + rw073_queue_tag(mq, msg));
    external_messages.enqueue({mq, msg, jam, requeue_if_blocked, -1});
}

void ultramodern::enqueue_external_pi_dma_message(PTR(OSMesgQueue) mq, OSMesg msg, bool jam) {
    // Preserve the existing PI requeue policy while retaining that this message
    // originated at synchronous PI-DMA completion, not OS_EVENT_PI delivery.
    external_messages.enqueue({mq, msg, jam, false, 7});
}

std::string rw073_queue_tag(PTR(OSMesgQueue) mq, OSMesg msg) {
    std::ostringstream detail;
    detail << "queue=0x" << std::uppercase << std::hex << static_cast<unsigned>(mq)
           << " msg=0x" << static_cast<unsigned>(msg);
    return detail.str();
}

std::string rw073_thread_tag(PTR(OSThread) thread) {
    std::ostringstream detail;
    detail << "thread=0x" << std::uppercase << std::hex << static_cast<unsigned>(thread);
    return detail.str();
}
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
static bool rw027_descriptor_trace_enabled() {
    return std::getenv("XR64_RW027_DESCRIPTOR_TRACE") != nullptr;
}

static bool rw027_descriptor_queue(PTR(OSMesgQueue) mq) {
    return mq == static_cast<PTR(OSMesgQueue)>(0x800FD254U) ||
            mq == static_cast<PTR(OSMesgQueue)>(0x800FDD08U);
}
#endif

void dequeue_external_messages(RDRAM_ARG1) {
    QueuedMessage to_send;
    std::vector<QueuedMessage> requeued_messages{};
    while (external_messages.try_dequeue(to_send)) {
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
        const bool rw020_event_queue = to_send.mq == static_cast<PTR(OSMesgQueue)>(0x800FFC00U)
                && std::getenv("XR64_RW020_NATIVE_TRACE") != nullptr;
        OSMesgQueue* mq = TO_PTR(OSMesgQueue, to_send.mq);
        const int valid_before = rw020_event_queue ? mq->validCount : 0;
#endif
        const bool sent = do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false);
        if(to_send.trace_source==int(ultramodern::EventMessageSource::Si) && std::getenv("XR64_XR_SI_TRACE"))
            std::fprintf(stderr,"RW114_SI_DELIVERY queue=%08X sent=%d retained=%d\n",unsigned(to_send.mq),int(sent),int(to_send.requeue_if_blocked));

    ultramodern::runtime_trace(
            "stage=message-delivery source=external sent=" + std::to_string(sent ? 1 : 0) +
            " " + rw073_queue_tag(to_send.mq, to_send.mesg));
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
        if (rw020_event_queue) {
            const char* source = "external";
            switch (to_send.trace_source) {
                case static_cast<int>(ultramodern::EventMessageSource::Timer): source = "timer"; break;
                case static_cast<int>(ultramodern::EventMessageSource::Sp): source = "sp"; break;
                case static_cast<int>(ultramodern::EventMessageSource::Si): source = "si"; break;
                case static_cast<int>(ultramodern::EventMessageSource::Ai): source = "ai"; break;
                case static_cast<int>(ultramodern::EventMessageSource::Vi): source = "vi"; break;
                case static_cast<int>(ultramodern::EventMessageSource::Pi): source = "event_pi"; break;
                case static_cast<int>(ultramodern::EventMessageSource::Dp): source = "dp"; break;
                case 7: source = "pi_dma_complete"; break;
            }
            std::fprintf(stderr, "RW020_EVENT8_DELIVERY source=%s queue=0x%08X msg=0x%08X valid_before=%d valid_after=%d sent=%d current_thread=0x%08X\n",
                    source, static_cast<unsigned>(to_send.mq), static_cast<unsigned>(to_send.mesg), valid_before,
                    mq->validCount, sent, static_cast<unsigned>(ultramodern::this_thread()));
            std::fflush(stderr);
        }
#endif
        if (!sent && to_send.requeue_if_blocked) {
            requeued_messages.push_back(to_send);
        }
    }
    for (QueuedMessage& cur_mesg : requeued_messages) {
        external_messages.enqueue(cur_mesg);
    }
}
void ultramodern::wait_for_external_message(RDRAM_ARG1) {
    QueuedMessage to_send;
    external_messages.wait_dequeue(to_send);
    if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false) && to_send.requeue_if_blocked) {
        external_messages.enqueue(to_send);
    }
}

void ultramodern::wait_for_external_message_timed(RDRAM_ARG u32 millis) {
    QueuedMessage to_send;
    if (external_messages.wait_dequeue_timed(to_send, std::chrono::milliseconds{millis})) {
        if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false) && to_send.requeue_if_blocked) {
            external_messages.enqueue(to_send);
        }
    }
}

extern "C" void osCreateMesgQueue(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg, s32 count) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    ultramodern::thread_wait_queue_reset(
            PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Receive);
    ultramodern::thread_wait_queue_reset(
            PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Send);
    mq->blocked_on_recv = NULLPTR;
    mq->blocked_on_send = NULLPTR;
    mq->msgCount = count;
    mq->msg = msg;
    mq->validCount = 0;
    mq->first = 0;
}

s32 MQ_GET_COUNT(OSMesgQueue *mq) {
    return mq->validCount;
}

s32 MQ_IS_EMPTY(OSMesgQueue *mq) {
    return mq->validCount == 0;
}

s32 MQ_IS_FULL(OSMesgQueue* mq) {
    return MQ_GET_COUNT(mq) >= mq->msgCount;
}

bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    const int rw073_valid_before = mq->validCount;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    const int rw027_valid_before = rw027_descriptor_trace_enabled() && rw027_descriptor_queue(mq_)
            ? mq->validCount : 0;
#endif
    if (!block) {
        // If non-blocking, fail if the queue is full.
        if (MQ_IS_FULL(mq)) {
            return false;
        }
    }
    else {
        // Otherwise, yield this thread until the queue has room.
        while (MQ_IS_FULL(mq)) {
            debug_printf("[Message Queue] Thread %d is blocked on send\n", TO_PTR(OSThread, ultramodern::this_thread())->id);
            ultramodern::runtime_trace(
                    "stage=thread-wait kind=send valid=" + std::to_string(mq->validCount) +
                    " " + rw073_queue_tag(mq_, msg) + " " +
                    rw073_thread_tag(ultramodern::this_thread()));
            ultramodern::thread_wait_queue_insert(
                    PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Send,
                    ultramodern::this_thread());
            ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
        }
    }
    
    if (jam) {
        // Jams insert at the head of the message queue's buffer.
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        TO_PTR(OSMesg, mq->msg)[mq->first] = msg;
        mq->validCount++;
    }
    else {
        // Sends insert at the tail of the message queue's buffer.
        s32 last = (mq->first + mq->validCount) % mq->msgCount;
        TO_PTR(OSMesg, mq->msg)[last] = msg;
        mq->validCount++;
    }

    PTR(OSThread) awakened_receiver = NULLPTR;
    // If any threads were blocked on receiving from this message queue, pop the first one and schedule it.
    if (!ultramodern::thread_wait_queue_empty(
            PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Receive)) {
        awakened_receiver = ultramodern::thread_wait_queue_pop(
                PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Receive);
        ultramodern::runtime_trace(
                "stage=thread-wakeup kind=receive " + rw073_queue_tag(mq_, msg) +
                " " + rw073_thread_tag(awakened_receiver) +
                " valid_before=" + std::to_string(rw073_valid_before) +
                " valid_after=" + std::to_string(mq->validCount));
        ultramodern::schedule_running_thread(PASS_RDRAM awakened_receiver);
    }
    ultramodern::runtime_trace(
            "stage=queue-send " + rw073_queue_tag(mq_, msg) +
            " valid_before=" + std::to_string(rw073_valid_before) +
            " valid_after=" + std::to_string(mq->validCount) +
            " current=" + rw073_thread_tag(ultramodern::this_thread()) +
            " awakened=" + rw073_thread_tag(awakened_receiver));
    #if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw027_descriptor_trace_enabled() && rw027_descriptor_queue(mq_)) {
        std::fprintf(stderr,
                "RW027_QUEUE_SEND queue=0x%08X msg=0x%08X valid_before=%d valid_after=%d current_thread=0x%08X\n",
                static_cast<unsigned>(mq_), static_cast<unsigned>(msg), rw027_valid_before,
                mq->validCount, static_cast<unsigned>(ultramodern::this_thread()));
        std::fflush(stderr);
    }
#endif

    return true;
}

bool ultramodern::deliver_fault_message_and_wait(RDRAM_ARG PTR(OSMesgQueue) mq, OSMesg msg) {
    if (!do_send(PASS_RDRAM mq, msg, false, false)) {
        return false;
    }
    ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
    return true;
}

bool do_recv(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    const int rw073_valid_before = mq->validCount;
    if (!block) {
        // If non-blocking, fail if the queue is empty
        if (MQ_IS_EMPTY(mq)) {
            return false;
        }
    } else {
        // Otherwise, yield this thread in a loop until the queue is no longer full
        while (MQ_IS_EMPTY(mq)) {
            debug_printf("[Message Queue] Thread %d is blocked on receive\n", TO_PTR(OSThread, ultramodern::this_thread())->id);
            ultramodern::runtime_trace(
                    "stage=thread-wait kind=receive valid=" + std::to_string(mq->validCount) +
                    " " + rw073_queue_tag(mq_, 0) + " " +
                    rw073_thread_tag(ultramodern::this_thread()));
            ultramodern::thread_wait_queue_insert(
                    PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Receive,
                    ultramodern::this_thread());
            ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
        }
    }

    if (msg_ != NULLPTR) {
        *TO_PTR(OSMesg, msg_) = TO_PTR(OSMesg, mq->msg)[mq->first];
    }
    
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;

    PTR(OSThread) awakened_sender = NULLPTR;
    // If any threads were blocked on sending to this message queue, pop the first one and schedule it.
    if (!ultramodern::thread_wait_queue_empty(
            PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Send)) {
        awakened_sender = ultramodern::thread_wait_queue_pop(
                PASS_RDRAM mq_, ultramodern::ThreadWaitQueue::Send);
        ultramodern::runtime_trace(
                "stage=thread-wakeup kind=send " + rw073_queue_tag(mq_, 0) +
                " " + rw073_thread_tag(awakened_sender) +
                " valid=" + std::to_string(mq->validCount));
        ultramodern::schedule_running_thread(PASS_RDRAM awakened_sender);
    }
    ultramodern::runtime_trace(
            "stage=queue-recv " + rw073_queue_tag(mq_, msg_ != NULLPTR ? *TO_PTR(OSMesg, msg_) : 0) +
            " valid_before=" + std::to_string(rw073_valid_before) +
            " valid_after=" + std::to_string(mq->validCount) +
            " current=" + rw073_thread_tag(ultramodern::this_thread()) +
            " awakened=" + rw073_thread_tag(awakened_sender));
    return true;
}

extern "C" s32 osSendMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    bool jam = false;
    
    // Don't directly send to the message queue if this isn't a game thread to avoid contention.
    if (!ultramodern::is_game_thread()) {
        ultramodern::enqueue_external_message(mq_, msg, jam, false);
        return 0;
    }
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to send the message.
    bool sent = do_send(PASS_RDRAM mq_, msg, jam, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return sent ? 0 : -1;
}

extern "C" s32 osJamMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    bool jam = true;
    
    // Don't directly send to the message queue if this isn't a game thread to avoid contention.
    if (!ultramodern::is_game_thread()) {
        ultramodern::enqueue_external_message(mq_, msg, jam, false);
        return 0;
    }
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to send the message.
    bool sent = do_send(PASS_RDRAM mq_, msg, jam, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return sent ? 0 : -1;
}

extern "C" s32 osRecvMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    
    assert(ultramodern::is_game_thread() && "RecvMesg not allowed outside of game threads.");
    
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    const bool rw019_event_queue = mq_ == static_cast<PTR(OSMesgQueue)>(0x800FFC00U)
            && std::getenv("XR64_RW019_NATIVE_TRACE") != nullptr;
    const bool rw020_event_queue = mq_ == static_cast<PTR(OSMesgQueue)>(0x800FFC00U)
            && std::getenv("XR64_RW020_NATIVE_TRACE") != nullptr;
    if (rw019_event_queue) {
        std::fprintf(stderr, "RW019_EVENT8_RECV phase=before_external thread=0x%08X valid=%d block=%d\n",
                static_cast<unsigned>(ultramodern::this_thread()), mq->validCount, flags == OS_MESG_BLOCK);
    }
#endif
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw019_event_queue) {
        std::fprintf(stderr, "RW019_EVENT8_RECV phase=after_external thread=0x%08X valid=%d\n",
                static_cast<unsigned>(ultramodern::this_thread()), mq->validCount);
        std::fflush(stderr);
    }

    if (rw020_event_queue) {
        std::fprintf(stderr, "RW020_EVENT8_RECV phase=before_receive queue=0x%08X valid=%d block=%d current_thread=0x%08X\n",
                static_cast<unsigned>(mq_), mq->validCount, flags == OS_MESG_BLOCK,
                static_cast<unsigned>(ultramodern::this_thread()));
        std::fflush(stderr);
    }
#endif
    // Try to receive a message.
    bool received = do_recv(PASS_RDRAM mq_, msg_, flags == OS_MESG_BLOCK);
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw027_descriptor_trace_enabled() && rw027_descriptor_queue(mq_)) {
        const OSMesg received_message = received && msg_ != NULLPTR ? *TO_PTR(OSMesg, msg_) : 0;
        std::fprintf(stderr,
                "RW027_QUEUE_RECV queue=0x%08X msg=0x%08X received=%d valid_after=%d current_thread=0x%08X\n",
                static_cast<unsigned>(mq_), static_cast<unsigned>(received_message), received,
                mq->validCount, static_cast<unsigned>(ultramodern::this_thread()));
        std::fflush(stderr);
    }
#endif
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw019_event_queue) {
        std::fprintf(stderr, "RW019_EVENT8_RECV phase=after_receive thread=0x%08X received=%d valid=%d\n",
                static_cast<unsigned>(ultramodern::this_thread()), received, mq->validCount);
        std::fflush(stderr);
    }
    
    if (rw020_event_queue) {
        const OSMesg received_message = received && msg_ != NULLPTR ? *TO_PTR(OSMesg, msg_) : 0;
        std::fprintf(stderr, "RW020_EVENT8_RECV phase=after_receive received=%d msg=0x%08X valid=%d current_thread=0x%08X\n",
                received, static_cast<unsigned>(received_message), mq->validCount,
                static_cast<unsigned>(ultramodern::this_thread()));
        std::fflush(stderr);
    }
#endif
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return received ? 0 : -1;
}
