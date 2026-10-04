#include "ultramodern/ultramodern.hpp"

#include "helpers.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#define MAXCONTROLLERS 4

namespace {

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
std::atomic<unsigned long long> g_rw017_cont_sequence{0};

bool rw017_trace_enabled() {
    const char* value = std::getenv("XR64_RW017_TRACE");
    return value != nullptr && value[0] == '1';
}

void rw017_trace_api(const char* operation, recomp_context* ctx, const char* detail) {
    if (!rw017_trace_enabled()) return;
    std::fprintf(stderr,
            "RW017_GUEST_CONTROLLER sequence=%llu source=guest operation=%s ra=0x%08X %s\n",
            ++g_rw017_cont_sequence, operation, static_cast<unsigned>(ctx->r31), detail);
    std::fflush(stderr);
}
#endif

} // namespace

extern "C" void recomp_set_current_frame_poll_id(uint8_t* rdram, recomp_context* ctx) {
    // TODO reimplement the system for tagging polls with IDs to handle games with multithreaded input polling.
}

extern "C" void recomp_measure_latency(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::measure_input_latency();
}

extern "C" void osContInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    PTR(u8) bitpattern = _arg<1, PTR(u8)>(rdram, ctx);
    PTR(OSContStatus) data = _arg<2, PTR(OSContStatus)>(rdram, ctx);
    u8 bitpattern_local = 0;

    s32 ret = osContInit(PASS_RDRAM mq, &bitpattern_local, data);

    MEM_B(0, bitpattern) = bitpattern_local;

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    char detail[192]{};
    std::snprintf(detail, sizeof(detail),
            "result=%d bitpattern=0x%02X channel=0 type=0x%04X status=0x%02X errno=0x%02X raw_request=00 raw_response=%02X %02X %02X",
            ret, static_cast<unsigned>(bitpattern_local),
            static_cast<unsigned>(MEM_HU(0, data)), static_cast<unsigned>(MEM_BU(2, data)),
            static_cast<unsigned>(MEM_BU(3, data)), static_cast<unsigned>(MEM_HU(0, data) & 0xFF),
            static_cast<unsigned>((MEM_HU(0, data) >> 8) & 0xFF),
            static_cast<unsigned>(MEM_BU(2, data)));
    rw017_trace_api("osContInit", ctx, detail);
#endif
    _return<s32>(ctx, ret);
}

extern "C" void osContReset_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    PTR(OSContStatus) data = _arg<1, PTR(OSContStatus)>(rdram, ctx);

    s32 ret = osContReset(PASS_RDRAM mq, data);

    _return<s32>(ctx, ret);
}

extern "C" void osContStartReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);

    s32 ret = osContStartReadData(PASS_RDRAM mq);

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    char detail[96]{};
    std::snprintf(detail, sizeof(detail), "result=%d si_completion=sent raw_request=01", ret);
    rw017_trace_api("osContStartReadData", ctx, detail);
#endif

    _return<s32>(ctx, ret);
}

extern "C" void osContGetReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSContPad) data = _arg<0, PTR(OSContPad)>(rdram, ctx);

    OSContPad dummy_data[MAXCONTROLLERS];

    osContGetReadData(dummy_data);

    for (int controller = 0; controller < MAXCONTROLLERS; controller++) {
        if (dummy_data[controller].err_no == 0) {
            MEM_H(6 * controller + 0, data) = dummy_data[controller].button;
            MEM_B(6 * controller + 2, data) = dummy_data[controller].stick_x;
            MEM_B(6 * controller + 3, data) = dummy_data[controller].stick_y;
            MEM_B(6 * controller + 4, data) = dummy_data[controller].err_no;
        }
    }
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    char detail[192]{};
    if (dummy_data[0].err_no == 0) {
        std::snprintf(detail, sizeof(detail),
                "channel=0 errno=0 buttons=0x%04X stick_x=%d stick_y=%d raw_request=01 raw_response=%02X %02X %02X %02X",
                static_cast<unsigned>(dummy_data[0].button), static_cast<int>(dummy_data[0].stick_x),
                static_cast<int>(dummy_data[0].stick_y),
                static_cast<unsigned>((dummy_data[0].button >> 8) & 0xFF),
                static_cast<unsigned>(dummy_data[0].button & 0xFF),
                static_cast<unsigned>(static_cast<unsigned char>(dummy_data[0].stick_x)),
                static_cast<unsigned>(static_cast<unsigned char>(dummy_data[0].stick_y)));
    } else {
        std::snprintf(detail, sizeof(detail), "channel=0 errno=0x%02X raw_request=01 raw_response=NO_RESPONSE",
                static_cast<unsigned>(dummy_data[0].err_no));
    }
    rw017_trace_api("osContGetReadData", ctx, detail);
#endif
}

extern "C" void osContStartQuery_recomp(uint8_t * rdram, recomp_context * ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);

    s32 ret = osContStartQuery(PASS_RDRAM mq);

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    char detail[96]{};
    std::snprintf(detail, sizeof(detail), "result=%d si_completion=sent raw_request=00", ret);
    rw017_trace_api("osContStartQuery", ctx, detail);
#endif

    _return<s32>(ctx, ret);
}

extern "C" void osContGetQuery_recomp(uint8_t * rdram, recomp_context * ctx) {
    PTR(OSContStatus) data = _arg<0, PTR(OSContStatus)>(rdram, ctx);

    osContGetQuery(PASS_RDRAM data);

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    char detail[192]{};
    std::snprintf(detail, sizeof(detail),
            "channel=0 type=0x%04X status=0x%02X errno=0x%02X raw_request=00 raw_response=%02X %02X %02X",
            static_cast<unsigned>(MEM_HU(0, data)), static_cast<unsigned>(MEM_BU(2, data)),
            static_cast<unsigned>(MEM_BU(3, data)), static_cast<unsigned>(MEM_HU(0, data) & 0xFF),
            static_cast<unsigned>((MEM_HU(0, data) >> 8) & 0xFF),
            static_cast<unsigned>(MEM_BU(2, data)));
    rw017_trace_api("osContGetQuery", ctx, detail);
#endif
}

extern "C" void osContSetCh_recomp(uint8_t* rdram, recomp_context* ctx) {
    u8 ch = _arg<0, u8>(rdram, ctx);

    s32 ret = osContSetCh(PASS_RDRAM ch);

    _return<s32>(ctx, ret);
}

extern "C" void __osMotorAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSPfs) pfs = _arg<0, PTR(OSPfs)>(rdram, ctx);
    s32 flag = _arg<1, s32>(rdram, ctx);

    s32 ret = __osMotorAccess(PASS_RDRAM pfs, flag);

    _return<s32>(ctx, ret);
}

extern "C" void osMotorInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    PTR(OSPfs) pfs = _arg<1, PTR(OSPfs)>(rdram, ctx);
    int channel = _arg<2, s32>(rdram, ctx);

    s32 ret = osMotorInit(PASS_RDRAM mq, pfs, channel);

    _return<s32>(ctx, ret);
}

extern "C" void osMotorStart_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSPfs) pfs = _arg<0, PTR(OSPfs)>(rdram, ctx);

    s32 ret = osMotorStart(PASS_RDRAM pfs);

    _return<s32>(ctx, ret);
}

extern "C" void osMotorStop_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(OSPfs) pfs = _arg<0, PTR(OSPfs)>(rdram, ctx);

    s32 ret = osMotorStop(PASS_RDRAM pfs);

    _return<s32>(ctx, ret);
}
