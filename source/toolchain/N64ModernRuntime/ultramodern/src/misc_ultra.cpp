#include "ultramodern/ultramodern.hpp"

#include <mutex>

namespace {
std::mutex runtime_trace_mutex;
ultramodern::runtime_trace_callback_t runtime_trace_callback = nullptr;
}

void ultramodern::set_runtime_trace_callback(runtime_trace_callback_t callback) {
    std::lock_guard lock(runtime_trace_mutex);
    runtime_trace_callback = callback;
}

void ultramodern::runtime_trace(std::string_view detail) {
    runtime_trace_callback_t callback = nullptr;
    {
        std::lock_guard lock(runtime_trace_mutex);
        callback = runtime_trace_callback;
    }
    if (callback != nullptr) {
        callback(detail);
    }
}

#include "ultramodern/ultra64.h"

#define K0BASE        0x80000000
#define K1BASE        0xA0000000
#define K2BASE        0xC0000000
#define IS_KSEG0(x)   ((u32)(x) >= K0BASE && (u32)(x) < K1BASE)
#define IS_KSEG1(x)   ((u32)(x) >= K1BASE && (u32)(x) < K2BASE)
#define K0_TO_PHYS(x) ((u32)(x)&0x1FFFFFFF)
#define K1_TO_PHYS(x) ((u32)(x)&0x1FFFFFFF)

u32 osVirtualToPhysical(PTR(void) addr) {
    uintptr_t addr_val = (uintptr_t)addr;
    if (IS_KSEG0(addr_val)) {
        return K0_TO_PHYS(addr_val);
    } else if (IS_KSEG1(addr_val)) {
        return K1_TO_PHYS(addr_val);
    } else {
        // TODO handle TLB mappings
        return (u32)addr_val;
    }
}

