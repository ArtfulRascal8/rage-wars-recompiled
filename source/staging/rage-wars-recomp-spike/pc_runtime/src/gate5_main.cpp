#include "rage_wars_audio_telemetry.hpp"
#include "rage_wars_crash_report.hpp"
#include "rage_wars_xr_guest_input.hpp"
extern "C" void xr64_page16_refresh_pc_options(unsigned char*);
#include "rage_wars_audio.hpp"
#ifdef XR64_DEMO_BUILD
#include "rage_wars_demo_launcher.hpp"
#include "rage_wars_demo_storage.hpp"
#endif
#include "xr64_fast3d/render_diagnostics.hpp"
#include "gate5_runtime.hpp"
#include "rage_wars_asset_loader.hpp"
#include "rage_wars_code_residency.hpp"
#include "rage_wars_scheduler.hpp"
#include "rage_wars_graphics_bridge.hpp"
#include "rage_wars_godot_live_backend.hpp"
#include "rdram_profile.hpp"
#include "rage_wars_input_replay.hpp"
#include "rage_wars_desktop_renderer.hpp"

#include "funcs.h"
#include <librecomp/game.hpp>
#include <librecomp/overlays.hpp>
#include <ultramodern/renderer_context.hpp>
#include <ultramodern/ultramodern.hpp>

#include <Windows.h>
#include <shellapi.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <process.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <rage_wars_controller_service.hpp>

extern RspExitReason rage_wars_audio_ucode(std::uint8_t*,std::uint32_t);

namespace {

extern "C" void osGetMemSize_recomp(std::uint8_t *, recomp_context *);
extern "C" void osPfsInitPak_recomp(std::uint8_t*, recomp_context*);
extern "C" void osPfsNumFiles_recomp(std::uint8_t*, recomp_context*);
extern "C" void osPfsFreeBlocks_recomp(std::uint8_t*, recomp_context*);

constexpr std::uint64_t kRomXxh3 = 0x21667BF153183FA0ULL;
constexpr std::uint32_t kEntrypoint = 0x80000400U;
constexpr std::uint32_t kMainHandoff = 0x002A70F0U;
constexpr std::uint32_t kIdleThreadEntry = 0x002933F8U;
constexpr std::uint32_t kMainThreadEntry = 0x00293420U;
constexpr std::uint32_t kCurrentThreadPhysical = 0x000D3F20U;
constexpr std::uint32_t kRw055MainThread = 0x801197C0U;
constexpr std::u8string_view kGameId = u8"xr64_rage_wars_us_v1_0";

LONG CALLBACK gate6_fault_locator(EXCEPTION_POINTERS *exception) {
    if (exception && exception->ExceptionRecord &&
            exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto rip = static_cast<std::uintptr_t>(exception->ContextRecord->Rip);
        const auto operation = exception->ExceptionRecord->ExceptionInformation[0];
        const auto address = exception->ExceptionRecord->ExceptionInformation[1];
        std::fprintf(stderr,
                "G6 fault rip=0x%llX rva=0x%llX operation=%llu address=0x%llX\n",
                static_cast<unsigned long long>(rip),
                static_cast<unsigned long long>(rip - base),
                static_cast<unsigned long long>(operation),
                static_cast<unsigned long long>(address));
        std::fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

struct Options {
    std::filesystem::path rom;
    std::filesystem::path data;
    std::filesystem::path trace;
    std::filesystem::path summary;
    std::filesystem::path input_replay;
    std::filesystem::path controller_pak;
    #ifdef XR64_DEMO_BUILD
    std::string stop_checkpoint = "none";
#else
    std::string stop_checkpoint = "main-handoff";
#endif
    std::uint64_t max_vi = 0;
    #ifdef XR64_DEMO_BUILD
    std::uint64_t max_seconds = 0;
#else
    std::uint64_t max_seconds = 10;
#endif
    #ifdef XR64_DEMO_BUILD
    bool headless = false;
#else
    bool headless = true;
#endif
    bool mute = false;
    #ifdef XR64_DEMO_BUILD
    bool visible = true;
#else
    bool visible = false;
#endif
    bool developer = false;
    bool skip_controller_pak_selftest = false;
    bool controller_no_pak = false;
    bool godot_live_bridge = false;
    bool attempt_xr = false;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    bool rw017_peripheral_trace = false;
    bool rw023_force_ares_producer_path = false;
#endif
};

struct State {
    std::mutex mutex;
    std::atomic<bool> gfx_initialized{false};
    std::atomic<bool> start_requested{false};
    std::atomic<bool> runtime_initialized{false};
    std::atomic<bool> rdram_allocated{false};
    std::atomic<bool> renderer_created{false};
    std::atomic<bool> generated_entrypoint_invoked{false};
    std::atomic<std::uint64_t> resident_main_handoff_count{0};
    std::atomic<bool> checkpoint_reached{false};
    std::atomic<bool> quit_requested{false};
    std::atomic<std::uint64_t> vi_count{0};
    std::atomic<std::uint64_t> thread_create_count{0};
    std::atomic<std::uint64_t> graphics_task_count{0};
    std::atomic<std::uint64_t> graphics_bridge_attempt_count{0};
    std::atomic<std::uint64_t> graphics_bridge_success_count{0};
    std::atomic<std::uint64_t> graphics_bridge_rejection_count{0};
    std::atomic<std::uint64_t> graphics_bridge_first_success_sequence{0};
    std::atomic<std::uint64_t> godot_live_frame_publish_count{0};
    std::atomic<std::uint64_t> godot_live_frame_last_task_sequence{0};
    std::atomic<std::uint64_t> rw044_display_list_builder_entries{0};
    std::atomic<std::uint64_t> rw044_ostask_builder_entries{0};
    std::atomic<std::uint64_t> rw044_rsp_task_start_entries{0};
    std::atomic<std::uint64_t> rw045_bootstrap_entries{0};
    std::atomic<std::uint64_t> rw045_main_thread_create_entries{0};
    std::atomic<std::uint64_t> rw045_display_list_callsite_entries{0};
    std::atomic<std::uint64_t> rw046_display_list_producer_entries{0};
    std::atomic<std::uint64_t> rw046_pre_call_gate_entries{0};
    std::atomic<std::uint64_t> rw047_funcs9_caller_entries{0};
    std::atomic<std::uint64_t> rw047_funcs9_call_gate_entries{0};
    std::atomic<std::uint64_t> rw047_funcs10_caller_entries{0};
    std::atomic<std::uint64_t> rw047_funcs10_mode_gate_entries{0};
    std::atomic<std::uint64_t> rw047_funcs10_counter_gate_entries{0};
    std::atomic<std::uint64_t> rw048_message_received_entries{0};
    std::atomic<std::uint64_t> rw048_message_classifier_entries{0};
    std::atomic<std::uint64_t> rw049_after_init_0_entries{0};
    std::atomic<std::uint64_t> rw049_after_init_1_entries{0};
    std::atomic<std::uint64_t> rw049_readiness_read_entries{0};
    std::atomic<std::uint64_t> rw049_ready_continuation_entries{0};
    std::atomic<std::uint64_t> rw049_after_virtual_init_entries{0};
    std::atomic<std::uint64_t> rw049_after_startup_service_entries{0};
    std::atomic<std::uint64_t> rw049_pre_initialize_entries{0};
    std::atomic<std::uint64_t> rw049_message_loop_entries{0};
    std::atomic<std::uint64_t> rw050_reset_state_entries{0};
    std::atomic<std::uint64_t> rw050_reset_complete_entries{0};
    std::atomic<std::uint64_t> rw050_indexed_init_complete_entries{0};
    std::atomic<std::uint64_t> rw050_static_init_complete_entries{0};
    std::atomic<std::uint64_t> rw050_service_init_complete_entries{0};
    std::atomic<std::uint64_t> rw050_config_group_a_entries{0};
    std::atomic<std::uint64_t> rw050_config_group_b_entries{0};
    std::atomic<std::uint64_t> rw050_final_setup_entries{0};
    std::atomic<std::uint64_t> rw051_chain_entry_entries{0};
    std::atomic<std::uint64_t> rw051_after_44e1d0_entries{0};
    std::atomic<std::uint64_t> rw051_after_404d84_entries{0};
    std::atomic<std::uint64_t> rw051_after_2a2134_entries{0};
    std::atomic<std::uint64_t> rw051_after_2982a8_entries{0};
    std::atomic<std::uint64_t> screen_update_count{0};
    std::atomic<std::uint64_t> malformed_task_count{0};
    std::atomic<bool> renderer_shutdown{false};
    std::atomic<bool> sp_dp_progress_observed{false};
    std::atomic<std::uint64_t> controller_pak_read_count{0};
    std::atomic<std::uint64_t> controller_pak_write_count{0};
    std::atomic<bool> controller_pak_selftest_passed{false};
    std::atomic<bool> controller_pak_selftest_executed{false};
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
        std::atomic<std::uint64_t> rw017_peripheral_sequence{0};
    std::atomic<std::uint64_t> rw073_order_sequence{0};
#endif
    std::uint8_t *rdram = nullptr;
    std::string status = "starting";
    std::string failure_code;
    std::string detail;
};

Options g_options;
State g_state;
std::ofstream g_trace;
rage_wars::RageWarsInputReplay g_input_replay;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
std::atomic<unsigned> g_rw023_forced_hook_count{0};
#endif

std::mutex g_controller_pak_mutex;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
std::atomic<bool> g_rw073_post_transaction_trace{false};
#endif
std::array<std::uint8_t, 32 * 1024> g_controller_pak{};
bool g_controller_pak_loaded = false;
bool g_controller_present = false;
std::atomic<bool> g_desktop_input_ready{false};
bool g_inherited_input_replay_cleared = false;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
std::atomic<bool> g_rw017_guest_execution_started{false};
#endif
std::string json_escape(std::string_view value) {
    std::string out;
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

void trace(std::string_view event, std::string_view detail = {}) {
    if (!::xr64::render_diagnostics::legacy()) return;
    std::lock_guard lock(g_state.mutex);
    if (!g_trace) return;
    g_trace << "{\"event\":\"" << json_escape(event) << "\",\"detail\":\""
            << json_escape(detail) << "\"}\n";
    g_trace.flush();
}

void trace_input_replay(
        const rage_wars::InputReplayTraceEvent& event) {
    std::ostringstream detail;
    detail << "elapsed_ms=" << event.elapsed_ms
           << " vi=" << event.vi
           << " button=" << event.button
           << " action=" << (event.pressed ? "press" : "release")
           << " button_mask=0x" << std::uppercase << std::hex
           << event.button_mask
           << " active_buttons=0x" << event.active_buttons;
    trace(event.pressed ? "rw076-input-press" : "rw076-input-release",
            detail.str());
    std::fprintf(stderr, "RW076_INPUT_%s %s",
            event.pressed ? "PRESS" : "RELEASE", detail.str().c_str());
    std::fflush(stderr);
}
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
std::atomic<unsigned> g_rw_replay_diag_lines{0};

void rw_replay_diag_emit(const std::string& line) {
    if (g_rw_replay_diag_lines.fetch_add(1, std::memory_order_relaxed) >= 256U) {
        return;
    }
    std::string terminated = line;
    terminated.push_back('\n');
    (void)std::fputs(terminated.c_str(), stderr);
}

void trace_input_replay_diagnostic(
        const rage_wars::InputReplayTraceEvent& event) {
    std::ostringstream line;
    line << "RW076_REPLAY_EVENT elapsed_ms=" << event.elapsed_ms
         << " vi=" << event.vi << " button=" << event.button
         << " action=" << (event.pressed ? "press" : "release")
         << " button_mask=0x" << std::uppercase << std::hex
         << event.button_mask << " active_buttons=0x" << event.active_buttons;
    rw_replay_diag_emit(line.str());
}
#endif

std::string hex_bytes(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) return "";
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        if (i != 0) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return out.str();
}

std::string hex32(std::uint32_t value); extern "C" void xr64_rw076_trace_callsite(std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc, const char *stage, std::uint32_t source_value) { if (ctx == nullptr) return; std::ostringstream detail; detail << std::uppercase << std::hex << std::setfill(char(48)) << "stage=" << stage << " guest_pc=0x" << std::setw(8) << guest_pc << " a0=0x" << std::setw(8) << static_cast<std::uint32_t>(ctx->r4) << " s1=0x" << std::setw(8) << static_cast<std::uint32_t>(ctx->r17) << " source_register=s1 source_value=0x" << std::setw(8) << source_value << " ra=0x" << std::setw(8) << static_cast<std::uint32_t>(ctx->r31) << " sp=0x" << std::setw(8) << static_cast<std::uint32_t>(ctx->r29) << " thread=0x" << std::setw(8) << static_cast<std::uint32_t>(ultramodern::this_thread()) << " entry=0x" << std::setw(8) << static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()); trace("rw076-callsite", detail.str()); }

extern "C" int xr64_rw076_duplicate_entrypoint_break() { return g_state.resident_main_handoff_count.load() > 1 ? 1 : 0; }
extern "C" void xr64_rw076_trace_null_offset_access(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc,
        std::uint32_t base, std::uint32_t offset,
        const char *base_register, const char *source_register) {
    if (ctx == nullptr || base != 0 || offset != 0x2E0U) {
        return;
    }

    std::ostringstream detail;
    detail << std::uppercase << std::hex << std::setfill('0')
           << "guest_pc=0x" << std::setw(8) << guest_pc
           << " instruction=lw_v1_0x2E0_" << base_register
           << " base_register=" << base_register
           << " base=0x" << std::setw(8) << base
           << " offset=0x" << std::setw(4) << offset
           << " effective_address=0x" << std::setw(8) << (base + offset)
           << " source_register=" << source_register
           << " ra=0x" << std::setw(8) << ctx->r31
           << " sp=0x" << std::setw(8) << ctx->r29
           << " gp=0x" << std::setw(8) << ctx->r28
           << " fp=0x" << std::setw(8) << ctx->r30
           << " v0=0x" << std::setw(8) << ctx->r2
           << " v1=0x" << std::setw(8) << ctx->r3
           << " a0=0x" << std::setw(8) << ctx->r4
           << " a1=0x" << std::setw(8) << ctx->r5
           << " a2=0x" << std::setw(8) << ctx->r6
           << " a3=0x" << std::setw(8) << ctx->r7
           << " t0=0x" << std::setw(8) << ctx->r8
           << " t1=0x" << std::setw(8) << ctx->r9
           << " t2=0x" << std::setw(8) << ctx->r10
           << " t3=0x" << std::setw(8) << ctx->r11
           << " t4=0x" << std::setw(8) << ctx->r12
           << " t5=0x" << std::setw(8) << ctx->r13
           << " t6=0x" << std::setw(8) << ctx->r14
           << " t7=0x" << std::setw(8) << ctx->r15
           << " t8=0x" << std::setw(8) << ctx->r24
           << " t9=0x" << std::setw(8) << ctx->r25
           << " s0=0x" << std::setw(8) << ctx->r16
           << " s1=0x" << std::setw(8) << ctx->r17
           << " s2=0x" << std::setw(8) << ctx->r18
           << " s3=0x" << std::setw(8) << ctx->r19
           << " s4=0x" << std::setw(8) << ctx->r20
           << " s5=0x" << std::setw(8) << ctx->r21
           << " s6=0x" << std::setw(8) << ctx->r22
           << " s7=0x" << std::setw(8) << ctx->r23
           << " current_thread=0x" << std::setw(8)
           << static_cast<std::uint32_t>(ultramodern::this_thread())
           << " current_entry=0x" << std::setw(8)
           << static_cast<std::uint32_t>(recomp::get_current_thread_entry_address());
    trace("rw076-null-offset-access", detail.str());
    std::fprintf(stderr, "RW076_NULL_OFFSET_ACCESS %s\n", detail.str().c_str());
    std::fflush(stderr);
}

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
void trace_rw073_order(std::string_view detail);
#endif
void rage_wars_thread_activated(std::uint8_t *rdram, PTR(OSThread) active_thread) {
    const auto activated = static_cast<std::uint32_t>(active_thread);
    std::memcpy(rdram + kCurrentThreadPhysical, &activated, sizeof(activated));

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    {
        std::ostringstream detail;
        detail << "stage=thread-activated thread=0x" << std::uppercase << std::hex
               << activated << " entry=0x"
               << static_cast<std::uint32_t>(recomp::get_current_thread_entry_address());
        if (g_rw073_post_transaction_trace.load()) {
            trace_rw073_order(detail.str());
        }
    }
#endif

    static std::atomic<bool> witnessed_rw055_main{false};
    if (activated != kRw055MainThread || witnessed_rw055_main.exchange(true)) {
        return;
    }

    std::uint32_t mirrored = 0;
    std::memcpy(&mirrored, rdram + kCurrentThreadPhysical, sizeof(mirrored));
    std::ostringstream detail;
    detail << "activated=" << hex32(activated)
           << " guest_current_thread=" << hex32(mirrored)
           << " physical=" << hex32(kCurrentThreadPhysical)
           << " invariant=" << (activated == mirrored ? "pass" : "fail");
    trace("rw057-thread-activation", detail.str());
    std::fprintf(stderr, "RW057_THREAD_ACTIVATION %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw044_producer_milestone(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    switch (guest_pc) {
        case 0x00292220U:
            counter = &g_state.rw044_display_list_builder_entries;
            stage = "display-list-builder";
            break;
        case 0x00291844U:
            counter = &g_state.rw044_ostask_builder_entries;
            stage = "ostask-builder";
            break;
        case 0x0028F970U:
            counter = &g_state.rw044_rsp_task_start_entries;
            stage = "rsp-task-start";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    const auto vi = g_state.vi_count.load();
    const auto thread = static_cast<std::uint32_t>(ultramodern::this_thread());
    const auto entry = static_cast<std::uint32_t>(recomp::get_current_thread_entry_address());
    const auto a0 = static_cast<std::uint32_t>(ctx->r4);
    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << vi << " thread=" << hex32(thread)
           << " entry=" << hex32(entry) << " a0=" << hex32(a0);
    trace("rw044-producer-milestone", detail.str());
    std::fprintf(stderr, "RW044_PRODUCER_MILESTONE %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw045_producer_transition(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    switch (guest_pc) {
        case 0x00292EACU:
            counter = &g_state.rw045_bootstrap_entries;
            stage = "main-thread-bootstrap";
            break;
        case 0x00292F0CU:
            counter = &g_state.rw045_main_thread_create_entries;
            stage = "main-thread-create";
            break;
        case 0x00292CDCU:
            counter = &g_state.rw045_display_list_callsite_entries;
            stage = "display-list-callsite";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " a0=" << hex32(static_cast<std::uint32_t>(ctx->r4))
           << " a1=" << hex32(static_cast<std::uint32_t>(ctx->r5))
           << " a2=" << hex32(static_cast<std::uint32_t>(ctx->r6))
           << " a3=" << hex32(static_cast<std::uint32_t>(ctx->r7));
    trace("rw045-producer-transition", detail.str());
    std::fprintf(stderr, "RW045_PRODUCER_TRANSITION %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw046_producer_transition(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    switch (guest_pc) {
        case 0x00292818U:
            counter = &g_state.rw046_display_list_producer_entries;
            stage = "display-list-producer-entry";
            break;
        case 0x00292CB4U:
            counter = &g_state.rw046_pre_call_gate_entries;
            stage = "pre-call-value-gate";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " beq_taken=" << (ctx->r2 == 0 ? "true" : "false");
    trace("rw046-producer-transition", detail.str());
    std::fprintf(stderr, "RW046_PRODUCER_TRANSITION %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw047_caller_transition(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    std::string_view call_gate = "n/a";
    switch (guest_pc) {
        case 0x00291334U:
            counter = &g_state.rw047_funcs9_caller_entries;
            stage = "funcs9-caller-entry";
            break;
        case 0x00291678U:
            counter = &g_state.rw047_funcs9_call_gate_entries;
            stage = "funcs9-call-gate";
            call_gate = ctx->r3 != ctx->r2 ? "open" : "closed";
            break;
        case 0x00293734U:
            counter = &g_state.rw047_funcs10_caller_entries;
            stage = "funcs10-caller-entry";
            break;
        case 0x002937A4U:
            counter = &g_state.rw047_funcs10_mode_gate_entries;
            stage = "funcs10-mode-gate";
            call_gate = ctx->r3 != ctx->r2 ? "open" : "closed";
            break;
        case 0x002937B4U:
            counter = &g_state.rw047_funcs10_counter_gate_entries;
            stage = "funcs10-counter-gate";
            call_gate = ctx->r2 != 0 ? "open" : "closed";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " v1=" << hex32(static_cast<std::uint32_t>(ctx->r3))
           << " call_gate=" << call_gate;
    trace("rw047-caller-transition", detail.str());
    std::fprintf(stderr, "RW047_CALLER_TRANSITION %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw048_message_transition(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    std::string_view type2_route = "n/a";
    switch (guest_pc) {
        case 0x002915E4U:
            counter = &g_state.rw048_message_received_entries;
            stage = "message-received";
            break;
        case 0x002915ECU:
            counter = &g_state.rw048_message_classifier_entries;
            stage = "message-classifier";
            type2_route = ctx->r3 == ctx->r2 ? "selected" : "not-selected";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " message=" << hex32(static_cast<std::uint32_t>(ctx->r4))
           << " type=" << hex32(static_cast<std::uint32_t>(ctx->r3))
           << " classifier_v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " type2_route=" << type2_route;
    trace("rw048-message-transition", detail.str());
    std::fprintf(stderr, "RW048_MESSAGE_TRANSITION %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw049_startup_frontier(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    std::string_view readiness = "n/a";
    switch (guest_pc) {
        case 0x00291350U:
            counter = &g_state.rw049_after_init_0_entries;
            stage = "after-init-0";
            break;
        case 0x00291358U:
            counter = &g_state.rw049_after_init_1_entries;
            stage = "after-init-1";
            break;
        case 0x00291364U:
            counter = &g_state.rw049_readiness_read_entries;
            stage = "readiness-read";
            readiness = (ctx->r2 == 1 || ctx->r2 == 2) ? "continue" : "pause";
            break;
        case 0x00291380U:
            counter = &g_state.rw049_ready_continuation_entries;
            stage = "ready-continuation";
            break;
        case 0x00291398U:
            counter = &g_state.rw049_after_virtual_init_entries;
            stage = "after-virtual-init";
            break;
        case 0x002913A8U:
            counter = &g_state.rw049_after_startup_service_entries;
            stage = "after-startup-service";
            break;
        case 0x002913B0U:
            counter = &g_state.rw049_pre_initialize_entries;
            stage = "pre-initialize";
            break;
        case 0x002915C4U:
            counter = &g_state.rw049_message_loop_entries;
            stage = "message-loop-entered";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " v1=" << hex32(static_cast<std::uint32_t>(ctx->r3))
           << " readiness=" << readiness;
    trace("rw049-startup-frontier", detail.str());
    std::fprintf(stderr, "RW049_STARTUP_FRONTIER %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw050_initialization_frontier(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    switch (guest_pc) {
        case 0x002913E0U:
            counter = &g_state.rw050_reset_state_entries;
            stage = "reset-state";
            break;
        case 0x0029144CU:
            counter = &g_state.rw050_reset_complete_entries;
            stage = "reset-complete";
            break;
        case 0x002914B0U:
            counter = &g_state.rw050_indexed_init_complete_entries;
            stage = "indexed-init-complete";
            break;
        case 0x002914D8U:
            counter = &g_state.rw050_static_init_complete_entries;
            stage = "static-init-complete";
            break;
        case 0x002914E8U:
            counter = &g_state.rw050_service_init_complete_entries;
            stage = "service-init-complete";
            break;
        case 0x00291528U:
            counter = &g_state.rw050_config_group_a_entries;
            stage = "config-group-a";
            break;
        case 0x00291568U:
            counter = &g_state.rw050_config_group_b_entries;
            stage = "config-group-b";
            break;
        case 0x002915A8U:
            counter = &g_state.rw050_final_setup_entries;
            stage = "final-setup";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " v1=" << hex32(static_cast<std::uint32_t>(ctx->r3));
    trace("rw050-initialization-frontier", detail.str());
    std::fprintf(stderr, "RW050_INITIALIZATION_FRONTIER %s\n", detail.str().c_str());
    std::fflush(stderr);
}

extern "C" void xr64_rw051_config_chain_frontier(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    switch (guest_pc) {
        case 0x00291544U:
            counter = &g_state.rw051_chain_entry_entries;
            stage = "chain-entry";
            break;
        case 0x00291550U:
            counter = &g_state.rw051_after_44e1d0_entries;
            stage = "after-0044e1d0";
            break;
        case 0x00291558U:
            counter = &g_state.rw051_after_404d84_entries;
            stage = "after-00404d84";
            break;
        case 0x00291560U:
            counter = &g_state.rw051_after_2a2134_entries;
            stage = "after-002a2134";
            break;
        case 0x00291568U:
            counter = &g_state.rw051_after_2982a8_entries;
            stage = "after-002982a8";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " v1=" << hex32(static_cast<std::uint32_t>(ctx->r3));
    trace("rw051-config-chain-frontier", detail.str());
    std::fprintf(stderr, "RW051_CONFIG_CHAIN_FRONTIER %s\n", detail.str().c_str());
    std::fflush(stderr);
}

#if defined(XR64_RAGE_WARS_CLEAN_RUN)
#define trace_rw017_peripheral(...) ((void)0)
#else
void trace_rw017_peripheral(std::string detail) {
    if (!g_options.rw017_peripheral_trace) return;
    const auto sequence = ++g_state.rw017_peripheral_sequence;
    const auto source = g_rw017_guest_execution_started.load() ? "guest" : "runtime-internal";
    trace("rw017-peripheral",
            "sequence=" + std::to_string(sequence) +
            " vi=" + std::to_string(g_state.vi_count.load()) +
            " source=" + source + " " + std::move(detail));
}
#endif

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
void trace_rw073_order(std::string_view detail) {
    if (!g_options.rw017_peripheral_trace) return;
    const auto sequence = ++g_state.rw073_order_sequence;
    trace("rw073-order",
            "sequence=" + std::to_string(sequence) +
            " vi=" + std::to_string(g_state.vi_count.load()) +
            " " + std::string(detail));
}
void runtime_order_trace(std::string_view detail) {
    const bool successful_pak_io =
            detail.find("stage=pak-io") != std::string_view::npos &&
            detail.find("result=0") != std::string_view::npos;
    if (successful_pak_io) {
        g_rw073_post_transaction_trace = true;
    }
    const bool is_status_response =
            detail.find("stage=pif-response operation=status") != std::string_view::npos;
    const bool is_pif_pak_response =
            detail.find("stage=pif-response operation=pak-") != std::string_view::npos;
    const bool is_rw074_startup_response =
            detail.find("stage=rw074-save-startup") != std::string_view::npos;
    if (!g_rw073_post_transaction_trace.load() && !is_status_response &&
            !is_pif_pak_response && !is_rw074_startup_response) return;
    trace_rw073_order(detail);
}
#else
void runtime_order_trace(std::string_view) {}
#endif

bool guest_range_valid(std::uint32_t pointer, std::uint32_t size) {
    const std::uint64_t physical = pointer & 0x1FFFFFFFU;
    constexpr std::uint64_t rdram_size = xr64::rage_wars::memory_profile::kRdramSize;
    if (size == 0) return pointer == 0 || physical < rdram_size;
    return physical < rdram_size && size <= rdram_size - physical;
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

#if defined(XR64_RAGE_WARS_CLEAN_RUN)
#define rw019_native_trace_enabled() false
#define rw023_native_trace_enabled() false
#define rw019_final_scheduler_snapshot(...) ((void)0)
#else
bool rw019_native_trace_enabled() {
    const char *value = std::getenv("XR64_RW019_NATIVE_TRACE");
    return value != nullptr && *value != '\0';
}

bool rw023_native_trace_enabled() {
    const char *value = std::getenv("XR64_RW023_NATIVE_TRACE");
    return value != nullptr && *value != '\0';
}

void rw019_final_scheduler_snapshot(const char *phase) {
    if (!rw019_native_trace_enabled() || g_state.rdram == nullptr) return;
    const auto thread_at = [](std::uint32_t address) {
        return reinterpret_cast<OSThread *>(g_state.rdram +
                (address & xr64::rage_wars::memory_profile::kPhysicalMask));
    };
    const auto idle = thread_at(0x80119590U);
    const auto main = thread_at(0x801197C0U);
    std::ostringstream queue;
    const auto runnable_threads = rage_wars::scheduler().snapshot(
            rage_wars::RageWarsScheduler::running_queue_id);
    for (std::size_t index = 0; index < runnable_threads.size() && index < 16; ++index) {
        const auto current = runnable_threads[index];
        const auto thread = thread_at(static_cast<std::uint32_t>(current));
        if (index != 0) queue << ',';
        queue << hex32(static_cast<std::uint32_t>(current)) << ":id=" << thread->id
              << ":state=" << thread->state << ":pri=" << thread->priority;
    }
    std::fprintf(stderr,
            "RW019_FINAL phase=%s idle={state=%u pri=%d queue=%s next=%s} "
            "main={state=%u pri=%d queue=%s next=%s} run_queue=[%s]\n",
            phase, idle->state, idle->priority,
            hex32(static_cast<std::uint32_t>(idle->queue)).c_str(),
            hex32(static_cast<std::uint32_t>(idle->next)).c_str(),
            main->state, main->priority,
            hex32(static_cast<std::uint32_t>(main->queue)).c_str(),
            hex32(static_cast<std::uint32_t>(main->next)).c_str(), queue.str().c_str());
    std::fflush(stderr);
}
#endif
void trace_graphics_task(
        std::uint64_t sequence, std::uint64_t vi, std::uint32_t task_address, const OSTask &task) {
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw023_native_trace_enabled()) {
        std::fprintf(stderr,
                "RW023_GRAPHICS_TASK sequence=%llu vi=%llu task=0x%08X type=%u data=0x%08X size=%u\n",
                static_cast<unsigned long long>(sequence), static_cast<unsigned long long>(vi), task_address,
                task.t.type, static_cast<unsigned>(task.t.data_ptr), task.t.data_size);
        std::fflush(stderr);
    }
#endif
    if (!::xr64::render_diagnostics::legacy()) return;
    std::lock_guard lock(g_state.mutex);
    if (!g_trace) return;
    g_trace << "{\"event\":\"graphics-task\""
            << ",\"sequence\":" << sequence
            << ",\"vi_count\":" << vi
            << ",\"ostask_address\":\"" << hex32(task_address) << "\""
            << ",\"task_type\":" << task.t.type
            << ",\"flags\":" << task.t.flags
            << ",\"display_list_pointer\":\"" << hex32(static_cast<std::uint32_t>(task.t.data_ptr)) << "\""
            << ",\"display_list_size\":" << task.t.data_size
            << ",\"ucode_pointer\":\"" << hex32(static_cast<std::uint32_t>(task.t.ucode)) << "\""
            << ",\"ucode_size\":" << task.t.ucode_size
            << ",\"ucode_data_pointer\":\"" << hex32(static_cast<std::uint32_t>(task.t.ucode_data)) << "\""
            << ",\"ucode_data_size\":" << task.t.ucode_data_size
            << ",\"ucode_boot_pointer\":\"" << hex32(static_cast<std::uint32_t>(task.t.ucode_boot)) << "\""
            << ",\"ucode_boot_size\":" << task.t.ucode_boot_size
            << "}\n";
    g_trace.flush();
}

void fail(std::string code, std::string detail) {
    {
        std::lock_guard lock(g_state.mutex);
        if (g_state.failure_code.empty()) {
            g_state.status = "failed";
            g_state.failure_code = std::move(code);
            g_state.detail = std::move(detail);
        }
    }
    g_state.quit_requested = true;
    ultramodern::quit();
}

void request_success(std::string_view detail) {
    {
        std::lock_guard lock(g_state.mutex);
        g_state.status = "passed";
        g_state.detail = std::string(detail);
    }
    g_state.quit_requested = true;
    ultramodern::quit();
}

std::optional<std::uint64_t> parse_count(const char *text) {
    try {
        std::size_t used = 0;
        const auto value = std::stoull(text, &used, 10);
        if (used != std::string(text).size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

bool parse_args(int argc, char **argv, std::string &error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto path_value = [&](std::filesystem::path &out) {
            if (++i >= argc) return false;
            out = std::filesystem::path(argv[i]);
            return true;
        };
        auto count_value = [&](std::uint64_t &out) {
            if (++i >= argc) return false;
            const auto parsed = parse_count(argv[i]);
            if (!parsed) return false;
            out = *parsed;
            return true;
        };
        if (arg == "--rom") { if (!path_value(g_options.rom)) error = "--rom needs a path"; }
        else if (arg == "--trace") { if (!path_value(g_options.trace)) error = "--trace needs a path"; }
        else if (arg == "--summary") { if (!path_value(g_options.summary)) error = "--summary needs a path"; }
        else if (arg == "--input-replay") { if (!path_value(g_options.input_replay)) error = "--input-replay needs a path"; }
        else if (arg == "--controller-pak") { if (!path_value(g_options.controller_pak)) error = "--controller-pak needs a path"; }
        else if (arg == "--controller-no-pak") g_options.controller_no_pak = true;
        else if (arg == "--max-vi") { if (!count_value(g_options.max_vi)) error = "--max-vi needs an integer"; }
        else if (arg == "--max-seconds") { if (!count_value(g_options.max_seconds)) error = "--max-seconds needs an integer"; }
        else if (arg == "--stop-at-checkpoint") {
            if (++i >= argc) error = "--stop-at-checkpoint needs a name";
            else g_options.stop_checkpoint = argv[i];
        } else if (arg == "--headless") { g_options.headless = true; g_options.visible = false; }
        else if (arg == "--visible") { g_options.visible = true; g_options.headless = false; }
        else if (arg == "--mute") g_options.mute = true;
        else if (arg == "--developer") g_options.developer = true;
        else if (arg == "--skip-controller-pak-selftest") g_options.skip_controller_pak_selftest = true;
        else if (arg == "--godot-live-bridge") g_options.godot_live_bridge = true;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
        else if (arg == "--rw017-peripheral-trace") g_options.rw017_peripheral_trace = true;
        else if (arg == "--rw023-force-ares-producer-path") g_options.rw023_force_ares_producer_path = true;
#endif
        else error = "unknown option: " + arg;
        if (!error.empty()) return false;
    }
    if (g_options.rom.empty()) error = "--rom is required";
    else if (g_options.max_seconds == 0 && g_options.max_vi == 0) error = "at least one bounded stop must be nonzero";
    else if (g_options.stop_checkpoint != "none" &&
            g_options.stop_checkpoint != "main-handoff" &&
            g_options.stop_checkpoint != "first-vi" &&
            g_options.stop_checkpoint != "first-graphics-task" &&
            g_options.stop_checkpoint != "multiple-graphics-tasks") {
        error = "Gate 6 supports checkpoints: none, main-handoff, first-vi, first-graphics-task, multiple-graphics-tasks";
    } else if (!g_options.input_replay.empty() && !std::filesystem::is_regular_file(g_options.input_replay)) {
        error = "input replay file does not exist";
    }
    else if (!g_options.controller_pak.empty() && !std::filesystem::is_regular_file(g_options.controller_pak))
        error = "controller pak file does not exist";
    if (g_options.controller_no_pak && !g_options.controller_pak.empty()) {
        error = "--controller-no-pak cannot be combined with --controller-pak";
    }
    return error.empty();
}

bool load_controller_pak(std::string& error) {
    if (g_options.controller_no_pak) { g_controller_pak_loaded=false; return true; }
    if (g_options.controller_pak.empty()) return true;
    std::ifstream file(g_options.controller_pak, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != static_cast<std::streamoff>(g_controller_pak.size())) {
        error = "controller pak must be exactly 32,768 bytes";
        return false;
    }
    file.seekg(0);
    file.read(reinterpret_cast<char*>(g_controller_pak.data()), g_controller_pak.size());
    if (!file) {
        error = "controller pak could not be read";
        return false;
    }
    g_controller_pak_loaded = true;
    return true;
}
void write_summary() {
#ifdef XR64_DEMO_BUILD
    if(!g_options.headless) {
        if(!g_state.failure_code.empty())xr64::rage_wars::demo::show_error(g_state.detail.c_str());
        return;
    }
#endif
    std::string status;
    std::string failure;
    std::string detail;
    {
        std::lock_guard lock(g_state.mutex);
        status = g_state.status;
        failure = g_state.failure_code;
        detail = g_state.detail;
    }
    const auto physical = xr64::rage_wars::recomp::gate5_desktop_controller_snapshot();
    const bool replay_enabled = g_input_replay.enabled();
    const bool controller_connected = replay_enabled ? g_controller_present : physical.connected;
    std::ostringstream out;
    out << "{\n"
        << "  \"schema\": \"xr64.rage-wars.gate6-summary.v1\",\n"
        << "  \"status\": \"" << json_escape(status) << "\",\n"
        << "  \"failure_code\": \"" << json_escape(failure) << "\",\n"
        << "  \"detail\": \"" << json_escape(detail) << "\",\n"
        << "  \"rom_xxh3_64\": \"21667BF153183FA0\",\n"
        << "  \"internal_name\": \"Turok: Rage Wars    \",\n"
        << "  \"save_type\": \"Controller Pak\",\n"
        << "  \"controller_1\": \"" << (controller_connected ? "standard_gamepad" : "none") << "\",\n"
        << "  \"controller_input\": \"" << (replay_enabled ? "diagnostic_replay" : "physical_sdl") << "\",\n"
        << "  \"physical_controller_opened\": " << (physical.connected ? "true" : "false") << ",\n"
        << "  \"physical_controller_name\": \"" << json_escape(physical.device_name) << "\",\n"
        << "  \"controller_pak_present\": " << (g_controller_pak_loaded ? "true" : "false") << ",\n"
        << "  \"controller_pak_size\": " << (g_controller_pak_loaded ? g_controller_pak.size() : 0) << ",\n"
        << "  \"controller_pak_read_count\": " << g_state.controller_pak_read_count.load() << ",\n"
        << "  \"controller_pak_write_count\": " << g_state.controller_pak_write_count.load() << ",\n"
        << "  \"controller_pak_selftest_executed\": " << (g_state.controller_pak_selftest_executed ? "true" : "false") << ",\n"
        << "  \"controller_pak_selftest_passed\": " << (g_state.controller_pak_selftest_passed ? "true" : "false") << ",\n"
        << "  \"input_replay_enabled\": " << (g_input_replay.enabled() ? "true" : "false") << ",\n"
        << "  \"input_replay_event_count\": " << g_input_replay.event_count() << ",\n";
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    out
        << "  \"rw023_force_ares_producer_path\": " << (g_options.rw023_force_ares_producer_path ? "true" : "false") << ",\n"
        << "  \"rw023_forced_hook_count\": " << g_rw023_forced_hook_count.load() << ",\n";
#endif
    out
        << "  \"compressed_code\": false,\n"
        << "  \"entrypoint_address\": \"0x80000400\",\n"
        << "  \"overlay_sections_registered\": " << rage_wars_generated_section_count() << ",\n"
        << "  \"gfx_initialized\": " << (g_state.gfx_initialized ? "true" : "false") << ",\n"
        << "  \"start_requested_after_gfx_init\": " << (g_state.start_requested ? "true" : "false") << ",\n"
        << "  \"runtime_initialized\": " << (g_state.runtime_initialized ? "true" : "false") << ",\n"
        << "  \"rdram_allocated_and_protected\": " << (g_state.rdram_allocated ? "true" : "false") << ",\n"
        << "  \"renderer_context_created\": " << (g_state.renderer_created ? "true" : "false") << ",\n"
        << "  \"generated_entrypoint_invoked\": " << (g_state.generated_entrypoint_invoked ? "true" : "false") << ",\n"
        << "  \"checkpoint\": \"" << json_escape(g_options.stop_checkpoint) << "\",\n"
        << "  \"checkpoint_reached\": " << (g_state.checkpoint_reached ? "true" : "false") << ",\n"
        << "  \"vi_count\": " << g_state.vi_count.load() << ",\n"
        << "  \"graphics_task_count\": " << g_state.graphics_task_count.load() << ",\n"
        << "  \"graphics_bridge_attempt_count\": " << g_state.graphics_bridge_attempt_count.load() << ",\n"
        << "  \"graphics_bridge_success_count\": " << g_state.graphics_bridge_success_count.load() << ",\n"
        << "  \"graphics_bridge_rejection_count\": " << g_state.graphics_bridge_rejection_count.load() << ",\n"
        << "  \"graphics_bridge_first_success_sequence\": " << g_state.graphics_bridge_first_success_sequence.load() << ",\n"
        << "  \"godot_live_bridge_enabled\": " << (g_options.godot_live_bridge ? "true" : "false") << ",\n"
        << "  \"godot_live_frame_publish_count\": " << g_state.godot_live_frame_publish_count.load() << ",\n"
        << "  \"godot_live_frame_last_task_sequence\": " << g_state.godot_live_frame_last_task_sequence.load() << ",\n"
        << "  \"rw044_display_list_builder_entries\": " << g_state.rw044_display_list_builder_entries.load() << ",\n"
        << "  \"rw044_ostask_builder_entries\": " << g_state.rw044_ostask_builder_entries.load() << ",\n"
        << "  \"rw044_rsp_task_start_entries\": " << g_state.rw044_rsp_task_start_entries.load() << ",\n"
        << "  \"rw045_bootstrap_entries\": " << g_state.rw045_bootstrap_entries.load() << ",\n"
        << "  \"rw045_main_thread_create_entries\": " << g_state.rw045_main_thread_create_entries.load() << ",\n"
        << "  \"rw045_display_list_callsite_entries\": " << g_state.rw045_display_list_callsite_entries.load() << ",\n"
        << "  \"rw046_display_list_producer_entries\": " << g_state.rw046_display_list_producer_entries.load() << ",\n"
        << "  \"rw046_pre_call_gate_entries\": " << g_state.rw046_pre_call_gate_entries.load() << ",\n"
        << "  \"rw047_funcs9_caller_entries\": " << g_state.rw047_funcs9_caller_entries.load() << ",\n"
        << "  \"rw047_funcs9_call_gate_entries\": " << g_state.rw047_funcs9_call_gate_entries.load() << ",\n"
        << "  \"rw047_funcs10_caller_entries\": " << g_state.rw047_funcs10_caller_entries.load() << ",\n"
        << "  \"rw047_funcs10_mode_gate_entries\": " << g_state.rw047_funcs10_mode_gate_entries.load() << ",\n"
        << "  \"rw047_funcs10_counter_gate_entries\": " << g_state.rw047_funcs10_counter_gate_entries.load() << ",\n"
        << "  \"rw048_message_received_entries\": " << g_state.rw048_message_received_entries.load() << ",\n"
        << "  \"rw048_message_classifier_entries\": " << g_state.rw048_message_classifier_entries.load() << ",\n"
        << "  \"rw049_after_init_0_entries\": " << g_state.rw049_after_init_0_entries.load() << ",\n"
        << "  \"rw049_after_init_1_entries\": " << g_state.rw049_after_init_1_entries.load() << ",\n"
        << "  \"rw049_readiness_read_entries\": " << g_state.rw049_readiness_read_entries.load() << ",\n"
        << "  \"rw049_ready_continuation_entries\": " << g_state.rw049_ready_continuation_entries.load() << ",\n"
        << "  \"rw049_after_virtual_init_entries\": " << g_state.rw049_after_virtual_init_entries.load() << ",\n"
        << "  \"rw049_after_startup_service_entries\": " << g_state.rw049_after_startup_service_entries.load() << ",\n"
        << "  \"rw049_pre_initialize_entries\": " << g_state.rw049_pre_initialize_entries.load() << ",\n"
        << "  \"rw049_message_loop_entries\": " << g_state.rw049_message_loop_entries.load() << ",\n"
        << "  \"rw050_reset_state_entries\": " << g_state.rw050_reset_state_entries.load() << ",\n"
        << "  \"rw050_reset_complete_entries\": " << g_state.rw050_reset_complete_entries.load() << ",\n"
        << "  \"rw050_indexed_init_complete_entries\": " << g_state.rw050_indexed_init_complete_entries.load() << ",\n"
        << "  \"rw050_static_init_complete_entries\": " << g_state.rw050_static_init_complete_entries.load() << ",\n"
        << "  \"rw050_service_init_complete_entries\": " << g_state.rw050_service_init_complete_entries.load() << ",\n"
        << "  \"rw050_config_group_a_entries\": " << g_state.rw050_config_group_a_entries.load() << ",\n"
        << "  \"rw050_config_group_b_entries\": " << g_state.rw050_config_group_b_entries.load() << ",\n"
        << "  \"rw050_final_setup_entries\": " << g_state.rw050_final_setup_entries.load() << ",\n"
        << "  \"rw051_chain_entry_entries\": " << g_state.rw051_chain_entry_entries.load() << ",\n"
        << "  \"rw051_after_44e1d0_entries\": " << g_state.rw051_after_44e1d0_entries.load() << ",\n"
        << "  \"rw051_after_404d84_entries\": " << g_state.rw051_after_404d84_entries.load() << ",\n"
        << "  \"rw051_after_2a2134_entries\": " << g_state.rw051_after_2a2134_entries.load() << ",\n"
        << "  \"rw051_after_2982a8_entries\": " << g_state.rw051_after_2982a8_entries.load() << ",\n"
        << "  \"screen_update_count\": " << g_state.screen_update_count.load() << ",\n"
        << "  \"malformed_graphics_task_count\": " << g_state.malformed_task_count.load() << ",\n"
        << "  \"sp_dp_completion_progress_observed\": " << (g_state.sp_dp_progress_observed ? "true" : "false") << ",\n"
        << "  \"renderer_shutdown_cleanly\": " << (g_state.renderer_shutdown ? "true" : "false") << "\n"
        << "}\n";
    if (!g_options.summary.empty()) {
        std::filesystem::create_directories(g_options.summary.parent_path());
        std::ofstream file(g_options.summary, std::ios::binary | std::ios::trunc);
        file << out.str();
    }
    std::fputs(out.str().c_str(), stdout);
}

class HeadlessRendererContext final : public ultramodern::renderer::RendererContext {
public:
    explicit HeadlessRendererContext(std::uint8_t *rdram) :
            godot_backend_(g_options.godot_live_bridge ?
                    std::make_unique<xr64::rage_wars::GodotLiveFrameBackend>() : nullptr),
            graphics_bridge_(godot_backend_.get()) {
        g_state.renderer_created = true;
        g_state.rdram_allocated = rdram != nullptr;
    g_state.rdram = rdram;
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
        trace("renderer-created");
        if (g_options.visible) {
            std::string desktop_error;
            if (!xr64::rage_wars::recomp::initialize_gate5_desktop(desktop_error, g_options.attempt_xr)) {
                fail("desktop_renderer_init_failed", desktop_error);
            } else {
                g_desktop_input_ready = true;
#ifdef XR64_DEMO_BUILD
                if (g_options.attempt_xr &&
                        xr64::rage_wars::recomp::gate5_xr_presentation_mode() ==
                                xr64::rage_wars::recomp::XrPresentationMode::Desktop) {
                    const auto status =
                            xr64::rage_wars::recomp::gate5_xr_transition_status();
                    if (!status.empty()) {
                        xr64::rage_wars::demo::show_error(status.c_str());
                    }
                }
#endif
                const auto physical =
                        xr64::rage_wars::recomp::gate5_desktop_controller_snapshot();
                trace("rw077-sdl-controller-open",
                        "success=" + std::string(physical.connected ? "yes" : "no") +
                        " device=" + (physical.device_name.empty()
                                ? std::string("none") : physical.device_name));
            }
        }
        if (godot_backend_ != nullptr && !godot_backend_->valid()) {
            fail("godot_live_backend_init_failed", godot_backend_->initialization_error());
        }
    }
    bool valid() override {
        return godot_backend_ == nullptr || godot_backend_->valid();
    }
    bool update_config(const ultramodern::renderer::GraphicsConfig &, const ultramodern::renderer::GraphicsConfig &) override { return true; }
    void enable_instant_present() override {}
    void send_dl(const OSTask *task) override {
        if (!task) {
            ++g_state.malformed_task_count;
            fail("malformed_graphics_task", "renderer received a null OSTask");
            return;
        }
        const auto sequence = ++g_state.graphics_task_count;
        const auto task_address = static_cast<std::uint32_t>(ultramodern::renderer::get_current_rsp_task_address());
        const auto data_pointer = static_cast<std::uint32_t>(task->t.data_ptr);
        const auto ucode_pointer = static_cast<std::uint32_t>(task->t.ucode);
        const auto ucode_data_pointer = static_cast<std::uint32_t>(task->t.ucode_data);
        const bool valid_task = task->t.type == M_GFXTASK && task_address != 0 &&
                guest_range_valid(data_pointer, task->t.data_size) &&
                guest_range_valid(ucode_pointer, task->t.ucode_size) &&
                guest_range_valid(ucode_data_pointer, task->t.ucode_data_size);
        if (!valid_task) {
            ++g_state.malformed_task_count;
            std::ostringstream detail;
            detail << "bounded rejection for graphics task " << sequence << " at " << hex32(task_address);
            fail("malformed_graphics_task", detail.str());
            return;
        }
        trace_graphics_task(sequence, g_state.vi_count.load(), task_address, *task);
        xr64::rage_wars::LiveN64TaskContext live_task;
        live_task.sequence = sequence;
        live_task.rsp_task_address = task_address;
        live_task.task_data_address = data_pointer;
        live_task.task_data_size = static_cast<std::uint32_t>(task->t.data_size);
        live_task.microcode_address = ucode_pointer;
        live_task.microcode_size = static_cast<std::uint32_t>(task->t.ucode_size);
        live_task.microcode_data_address = ucode_data_pointer;
        live_task.microcode_data_size = static_cast<std::uint32_t>(task->t.ucode_data_size);
        live_task.rdram = g_state.rdram;
        live_task.rdram_size = xr64::rage_wars::memory_profile::kRdramSize;
        const auto bridge_result = graphics_bridge_.submit(live_task);
        ++g_state.graphics_bridge_attempt_count;
        if (bridge_result.translated) {
            ++g_state.graphics_bridge_success_count;
            std::uint64_t expected_first = 0;
            g_state.graphics_bridge_first_success_sequence.compare_exchange_strong(
                    expected_first, sequence);
            if (godot_backend_ != nullptr) {
                ++g_state.godot_live_frame_publish_count;
                g_state.godot_live_frame_last_task_sequence = sequence;
            }
        } else {
            ++g_state.graphics_bridge_rejection_count;
        }

        if (sequence >= 2) g_state.sp_dp_progress_observed = true;
    }
private:
    std::unique_ptr<xr64::rage_wars::GodotLiveFrameBackend> godot_backend_;
    xr64::rage_wars::RageWarsGraphicsBridge graphics_bridge_;
public:
        void send_dummy_workload(std::uint32_t) override {}
    void update_screen() override {
        ++g_state.screen_update_count;
        if (g_options.visible &&
                !xr64::rage_wars::recomp::pump_gate5_desktop_events()) {
            g_state.quit_requested = true;
            ultramodern::quit();
        }
    }
    bool has_independent_presentation() const override {
        return g_options.visible && xr64::rage_wars::recomp::gate5_independent_presentation();
    }
    void present_frame() override {
        // Pump even when the guest stops issuing VI screen updates.
        if (!xr64::rage_wars::recomp::pump_gate5_desktop_events()) {
            g_state.quit_requested = true;
            ultramodern::quit();
            return;
        }
        std::string error;
        if (!xr64::rage_wars::recomp::present_gate5_desktop(error)) {
            fail("xr_presentation_failed", error);
            ultramodern::quit();
        }
    }
    void shutdown() override {
        if (g_options.visible) {
            xr64::rage_wars::recomp::shutdown_gate5_desktop();
            g_desktop_input_ready = false;
        }
        g_state.renderer_shutdown = true;
        trace("renderer-shutdown", "renderer and SDL input stopped cleanly");
    }
    std::uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0F; }
};

std::unique_ptr<ultramodern::renderer::RendererContext> create_renderer(
        std::uint8_t *rdram, ultramodern::renderer::WindowHandle, bool) {
    return std::make_unique<HeadlessRendererContext>(rdram);
}

#ifdef XR64_AUDIO
RspUcodeFunc *get_rsp_microcode(const OSTask *task) {
    if(xr64::rage_wars::audio::enabled() && task->t.type==2 &&
       (task->t.ucode&0x1FFFFFFFU)==0x000D9EB0U && (task->t.ucode_data&0x1FFFFFFFU)==0x000DB4E0U) return rage_wars_audio_ucode;
    return nullptr;
}
#else
RspUcodeFunc *get_rsp_microcode(const OSTask*) { return nullptr; }
#endif
void queue_samples(std::int16_t *samples, std::size_t count) { if(!g_options.mute) xr64::rage_wars::audio::queue(samples,count); }
std::size_t frames_remaining() { return xr64::rage_wars::audio::remaining(); }
void set_frequency(std::uint32_t hz) { xr64::rage_wars::audio::frequency(hz); }
std::uint64_t input_replay_clock_ms() {
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool effective_controller_present() {
    if (g_input_replay.enabled() || !g_desktop_input_ready.load()) {
        return g_controller_present;
    }
    return xr64::rage_wars::recomp::gate5_desktop_controller_snapshot().connected;
}

bool input_replay_start_ready() {
    const bool controller_present = effective_controller_present();
    bool profile_gate_ready = false;
    std::uint64_t pak_read_count = 0;
    bool ready = false;
    if (controller_present) {
        if (!g_controller_pak_loaded) {
            profile_gate_ready = true;
            ready = true;
        } else {
            profile_gate_ready = rage_wars::controller_service().save_profile_ready();
            pak_read_count = g_state.controller_pak_read_count.load();
            ready = profile_gate_ready && pak_read_count != 0;
        }
    }
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
    static std::atomic<int> previous_gate{-1};
    const int gate_state = (controller_present ? 1 : 0) |
            (g_controller_pak_loaded ? 2 : 0) |
            (profile_gate_ready ? 4 : 0) |
            (pak_read_count != 0 ? 8 : 0) | (ready ? 16 : 0);
    if (previous_gate.exchange(gate_state, std::memory_order_relaxed) != gate_state) {
        std::ostringstream line;
        line << "RW076_REPLAY_GATE controller_present=" << controller_present
             << " pak_loaded=" << g_controller_pak_loaded
             << " profile_ready=" << profile_gate_ready
             << " pak_reads=" << pak_read_count << " ready=" << ready
             << " vi=" << g_state.vi_count.load();
        rw_replay_diag_emit(line.str());
    }
#endif
    return ready;
}

void poll_input() {}
bool get_input(int controller, std::uint16_t* buttons, float* x, float* y) {
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
    static std::atomic<std::uint64_t> diagnostic_poll_count{0};
    const std::uint64_t diagnostic_poll =
            diagnostic_poll_count.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (diagnostic_poll == 1U || diagnostic_poll % 300U == 0U) {
        std::ostringstream line;
        line << "RW076_REPLAY_POLL count=" << diagnostic_poll
             << " controller=" << controller
             << " buttons_ptr=" << (buttons != nullptr)
             << " x_ptr=" << (x != nullptr) << " y_ptr=" << (y != nullptr)
             << " vi=" << g_state.vi_count.load();
        rw_replay_diag_emit(line.str());
    }
#endif
    if (controller != 0 || buttons == nullptr || x == nullptr || y == nullptr) return false;
    if(g_state.rdram)xr64_page16_refresh_pc_options(g_state.rdram);
    // The game flag stays 1 on title and match-setup pages. A live menu manager
    // distinguishes those pages from actual gameplay (verified in title, pause,
    // and in-match RDRAM captures).
    const std::uint32_t menu_manager = g_state.rdram
            ? *reinterpret_cast<const std::uint32_t*>(g_state.rdram + 0x144E00U) : 0;
    const bool gameplay = g_state.rdram != nullptr &&
            g_state.rdram[0x140225U ^ 3U] == 1 &&
            *reinterpret_cast<const std::uint32_t*>(g_state.rdram + 0x1407D4U) == 0 &&
            menu_manager == 0;
#ifndef XR64_DEMO_BUILD
    static int last_input_context=-1;
    if(last_input_context!=int(gameplay)) {
        last_input_context=int(gameplay);
        std::fprintf(stderr,"RW105_INPUT_CONTEXT gameplay=%u pause=%u game=%u menu_manager=0x%08X\n",
                gameplay?1U:0U,g_state.rdram ? *reinterpret_cast<const std::uint32_t*>(g_state.rdram+0x1407D4U):99U,
                g_state.rdram ? g_state.rdram[0x140225U^3U]:99U,menu_manager);
    }
#endif
    const bool replay_enabled = g_input_replay.enabled();
    const auto physical = xr64::rage_wars::recomp::consume_gate5_desktop_controller_snapshot(gameplay,
                xr64::rage_wars::recomp::gate5_xr_guest_gameplay(g_state.rdram,gameplay));
    const bool present = replay_enabled ? g_controller_present : physical.connected;
    const bool valid = controller == 0 && present && buttons != nullptr &&
            x != nullptr && y != nullptr;
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
    static std::atomic<int> previous_poll_state{-1};
    const std::uint32_t diagnostic_pause = g_state.rdram
            ? *reinterpret_cast<const std::uint32_t*>(g_state.rdram + 0x1407D4U) : UINT32_MAX;
    const int poll_state = (replay_enabled ? 1 : 0) | (present ? 2 : 0) |
            (valid ? 4 : 0) | (g_input_replay.armed() ? 8 : 0) |
            (gameplay ? 16 : 0) | (diagnostic_pause ? 32 : 0) | (menu_manager ? 64 : 0);
    if (previous_poll_state.exchange(poll_state, std::memory_order_relaxed) != poll_state) {
        std::ostringstream line;
        line << "RW076_REPLAY_POLL_STATE enabled=" << replay_enabled
             << " present=" << present << " valid=" << valid
             << " armed=" << g_input_replay.armed()
             << " context_gameplay=" << gameplay << " observed_pause=" << diagnostic_pause
             << " observed_menu_manager=0x" << std::hex << menu_manager << std::dec
             << " pak_loaded=" << g_controller_pak_loaded
             << " pak_reads=" << g_state.controller_pak_read_count.load()
             << " vi=" << g_state.vi_count.load();
        rw_replay_diag_emit(line.str());
    }
#endif
    if (valid) {
        const auto now_ms = input_replay_clock_ms();
        const auto vi = g_state.vi_count.load();
        if (replay_enabled && !g_input_replay.armed() &&
                input_replay_start_ready()) {
            g_input_replay.arm(now_ms, vi);
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
            {
                std::ostringstream line;
                line << "RW076_REPLAY_ARM clock_ms=" << now_ms << " vi=" << vi
                     << " pak_reads=" << g_state.controller_pak_read_count.load();
                rw_replay_diag_emit(line.str());
            }
#endif
            trace("rw076-input-replay-armed",
                    "elapsed_ms=0 vi=" + std::to_string(vi));
        }
        if (replay_enabled) {
            *buttons = g_input_replay.sample(now_ms, vi, x, y);
        } else {
            *buttons = physical.buttons;
            *x = physical.stick_x;
            *y = physical.stick_y;
        }
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
        if (replay_enabled) {
            static std::mutex diagnostic_delivery_mutex;
            static bool have_previous_delivery = false;
            static std::uint16_t previous_buttons = 0;
            static int previous_x_milli = 0;
            static int previous_y_milli = 0;
            const int x_milli = static_cast<int>(std::lround(*x * 1000.0F));
            const int y_milli = static_cast<int>(std::lround(*y * 1000.0F));
            std::lock_guard delivery_lock(diagnostic_delivery_mutex);
            if (!have_previous_delivery || *buttons != previous_buttons ||
                    x_milli != previous_x_milli || y_milli != previous_y_milli) {
                std::ostringstream line;
                line << "RW076_REPLAY_DELIVER vi=" << vi << " buttons=0x"
                     << std::uppercase << std::hex << *buttons << std::dec
                     << " stick_x=" << *x << " stick_y=" << *y
                     << " context_gameplay=" << gameplay << " observed_pause=" << diagnostic_pause
                     << " observed_menu_manager=0x" << std::hex << menu_manager << std::dec;
                rw_replay_diag_emit(line.str());
                previous_buttons = *buttons;
                previous_x_milli = x_milli;
                previous_y_milli = y_milli;
                have_previous_delivery = true;
            }
        }
#endif
        if (xr64::rage_wars::audio::telemetry::enabled()) {
            xr64::rage_wars::audio::telemetry::input_observation(g_input_replay.armed(),
                rage_wars::controller_service().save_profile_ready(), *buttons, std::hypot(*x, *y) > 0.05F);
        }
#ifndef XR64_DEMO_BUILD
        static std::mutex transition_mutex;
        static std::uint16_t last_buttons = 0;
        static bool last_stick_active = false;
        std::lock_guard transition_lock(transition_mutex);
        const bool stick_active = std::hypot(*x, *y) > 0.05F;
        if (*buttons != last_buttons || stick_active != last_stick_active) {
            std::fprintf(stderr,
                    "RW077_GUEST_INPUT_DELIVERY mode=%s buttons=0x%04X stick=(%.3f,%.3f) vi=%llu\n",
                    replay_enabled ? "replay" : "live", *buttons, *x, *y,
                    static_cast<unsigned long long>(vi));
            std::fflush(stderr);
            last_buttons = *buttons;
            last_stick_active = stick_active;
        }
#endif
    }
    trace_rw017_peripheral(
            "layer=host-callback operation=controller-read channel=" + std::to_string(controller) +
            " buttons=" + (valid ? std::to_string(*buttons) : "unknown") +
            " stick_x=" + (valid ? std::to_string(*x) : "unknown") +
            " stick_y=" + (valid ? std::to_string(*y) : "unknown"));
    return valid;
}
void set_rumble(int, bool) {}
ultramodern::input::connected_device_info_t connected_device(int controller) {
    ultramodern::input::connected_device_info_t result{
            ultramodern::input::Device::None, ultramodern::input::Pak::None};
    if (controller == 0 && effective_controller_present()) {
        result = {ultramodern::input::Device::Controller, g_controller_pak_loaded ? ultramodern::input::Pak::ControllerPak : ultramodern::input::Pak::None};
    }
    const bool valid = result.connected_device == ultramodern::input::Device::Controller;
    const bool pak = result.connected_pak == ultramodern::input::Pak::ControllerPak;
    trace_rw017_peripheral(
            "layer=host-callback operation=controller-status channel=" + std::to_string(controller) +
            " command=00 request=00 response=" + (valid ? (pak ? "05 00 01" : "05 00 00") : "NO_RESPONSE") +
            " controller_type=" + (valid ? "0x0005" : "none") +
            " controller_status=" + (valid ? (pak ? "0x01" : "0x00") : "unknown") +
            " controller_errno=" + (valid ? "0" : "8") +
            " result=" + (valid ? "0" : "no_response"));
    return result;
}
bool controller_port_status(
        int controller, rage_wars::ControllerPortStatus* status) {
    if (status == nullptr) {
        return false;
    }
    const auto device = connected_device(controller);
    status->connected =
            device.connected_device == ultramodern::input::Device::Controller;
    status->controller_pak =
            device.connected_pak == ultramodern::input::Pak::ControllerPak;
    status->type = 0x0005;
    return status->connected;
}

void signal_controller_completion() {
    ultramodern::send_si_message();
}

bool read_controller_pak(int controller, std::size_t offset, std::uint8_t* data, std::size_t size) {
    std::lock_guard lock(g_controller_pak_mutex);
    const bool valid = controller == 0 && g_controller_pak_loaded && data != nullptr &&
            offset <= g_controller_pak.size() && size <= g_controller_pak.size() - offset;
    if (!valid) {
        trace_rw017_peripheral(
                "layer=host-callback operation=pak-read channel=" + std::to_string(controller) +
                " command=02 offset=" + std::to_string(offset) + " size=" + std::to_string(size) +
                " response=NO_RESPONSE result=failure");
        return false;
    }
    std::memcpy(data, g_controller_pak.data() + offset, size);
    ++g_state.controller_pak_read_count;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    ultramodern::runtime_trace("stage=pak-io operation=read channel=" + std::to_string(controller) +
            " offset=" + std::to_string(offset) + " size=" + std::to_string(size) + " result=0");
#endif
    trace_rw017_peripheral(
            "layer=host-callback operation=pak-read channel=" + std::to_string(controller) +
            " command=02 offset=" + std::to_string(offset) + " size=" + std::to_string(size) +
            " response_data=" + hex_bytes(data, size) + " result=success");
    return true;
}

bool write_controller_pak(
        int controller, std::size_t offset, const std::uint8_t* data, std::size_t size) {
    std::lock_guard lock(g_controller_pak_mutex);
    const bool valid = controller == 0 && g_controller_pak_loaded && data != nullptr &&
            offset <= g_controller_pak.size() && size <= g_controller_pak.size() - offset;
    if (!valid) {
        trace_rw017_peripheral(
                "layer=host-callback operation=pak-write channel=" + std::to_string(controller) +
                " command=03 offset=" + std::to_string(offset) + " size=" + std::to_string(size) +
                " request_data=UNAVAILABLE result=failure");
        return false;
    }
#ifdef XR64_DEMO_BUILD
    auto candidate = g_controller_pak;
    std::memcpy(candidate.data() + offset, data, size);
    std::string pak_error;
    if (!xr64::rage_wars::demo::save_controller_pak_atomic(
            g_options.controller_pak,candidate,pak_error)) {
        std::fprintf(stderr,"rage_wars: Controller Pak write failed: %s\n",pak_error.c_str());
        trace_rw017_peripheral(
                "layer=host-callback operation=pak-write channel=" + std::to_string(controller) +
                " command=03 offset=" + std::to_string(offset) + " size=" + std::to_string(size) +
                " request_data=" + hex_bytes(data, size) + " result=failure");
        return false;
    }
    g_controller_pak = candidate;
#else
    std::memcpy(g_controller_pak.data() + offset, data, size);
    std::ofstream file(g_options.controller_pak, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(g_controller_pak.data()), g_controller_pak.size());
    if (!file) {
        trace_rw017_peripheral(
                "layer=host-callback operation=pak-write channel=" + std::to_string(controller) +
                " command=03 offset=" + std::to_string(offset) + " size=" + std::to_string(size) +
                " request_data=" + hex_bytes(data, size) + " result=failure");
        return false;
    }
#endif
    ++g_state.controller_pak_write_count;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    ultramodern::runtime_trace("stage=pak-io operation=write channel=" + std::to_string(controller) +
            " offset=" + std::to_string(offset) + " size=" + std::to_string(size) + " result=0");
#endif
    trace_rw017_peripheral(
            "layer=host-callback operation=pak-write channel=" + std::to_string(controller) +
            " command=03 offset=" + std::to_string(offset) + " size=" + std::to_string(size) +
            " request_data=" + hex_bytes(data, size) + " result=success");
    return true;
}
bool validate_controller_pak_bridge() {
    g_state.controller_pak_selftest_executed = true;
    if (!g_controller_pak_loaded) return true;
    std::vector<std::uint8_t> validation_storage(xr64::rage_wars::memory_profile::kRdramSize);
    std::uint8_t* rdram = validation_storage.data();
    constexpr gpr pfs_address = static_cast<gpr>(static_cast<std::int32_t>(0x80001000U));
    constexpr gpr max_files_address = static_cast<gpr>(static_cast<std::int32_t>(0x80001100U));
    constexpr gpr files_used_address = static_cast<gpr>(static_cast<std::int32_t>(0x80001104U));
    constexpr gpr free_bytes_address = static_cast<gpr>(static_cast<std::int32_t>(0x80001108U));

    recomp_context init{};
    init.r5 = pfs_address;
    init.r6 = 0;
    osPfsInitPak_recomp(rdram, &init);

    recomp_context num{};
    num.r4 = pfs_address;
    num.r5 = max_files_address;
    num.r6 = files_used_address;
    osPfsNumFiles_recomp(rdram, &num);

    recomp_context free{};
    free.r4 = pfs_address;
    free.r5 = free_bytes_address;
    osPfsFreeBlocks_recomp(rdram, &free);

    auto* pfs = reinterpret_cast<OSPfs*>(rdram + 0x1000);
    const s32 max_files = MEM_W(0, max_files_address);
    const s32 files_used = MEM_W(0, files_used_address);
    const s32 free_bytes = MEM_W(0, free_bytes_address);
    const bool passed = init.r2 == 0 && num.r2 == 0 && free.r2 == 0 &&
            (pfs->status & 1) != 0 && pfs->banks == 1 &&
            max_files == 16 && files_used >= 0 && free_bytes > 0;
    g_state.controller_pak_selftest_passed = passed;
    std::fprintf(stderr,
            "RW_G2_PAK_SELFTEST passed=%s init=%lld num=%lld free=%lld status=%d banks=%u max=%d used=%d free_bytes=%d\n",
            passed ? "yes" : "no", static_cast<long long>(init.r2), static_cast<long long>(num.r2),
            static_cast<long long>(free.r2), pfs->status, pfs->banks, max_files, files_used, free_bytes);
    std::fflush(stderr);
    return passed;
}
void update_gfx(void *) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            g_state.quit_requested = true;
            ultramodern::quit();
            return;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
void message_box(const char *message) { fail("runtime_message", message ? message : "unknown runtime message"); }
std::string thread_name(const OSThread *) { return "RageWarsGuest"; }

void gfx_initialized() {
    if (g_state.gfx_initialized.exchange(true)) return;
    trace("gfx-initialized");
}


void vi_callback() {
    if (!g_state.start_requested.exchange(true)) {
        recomp::start_game(std::u8string(kGameId), "");
        trace("start-game-requested", "one-shot first valid VI after gfx initialization");
        return;
    }
    const auto count = ++g_state.vi_count;
#ifndef XR64_DEMO_BUILD
    static const bool rw108_vi_heartbeat=[] {
        const char* value=std::getenv("XR64_RW_VI_HEARTBEAT");
        return value && value[0]=='1';
    }();
    if(rw108_vi_heartbeat && count%120==0)
        std::fprintf(stderr,"RW108_VI_HEARTBEAT vi=%llu graphics_tasks=%llu\n",
                static_cast<unsigned long long>(count),
                static_cast<unsigned long long>(g_state.graphics_task_count.load()));
#endif
#ifndef XR64_DEMO_BUILD
    if (g_state.rdram != nullptr && g_desktop_input_ready.load()) {
        constexpr std::uint32_t raw_pad_physical = 0x00108EA0U;
        constexpr std::uint32_t processed_held_physical = 0x00109340U;
        const auto raw_buttons = *reinterpret_cast<const std::uint16_t *>(
                g_state.rdram + (raw_pad_physical ^ 2U));
        const auto raw_x = static_cast<std::int8_t>(
                g_state.rdram[(raw_pad_physical + 2U) ^ 3U]);
        const auto raw_y = static_cast<std::int8_t>(
                g_state.rdram[(raw_pad_physical + 3U) ^ 3U]);
        const auto processed_held = *reinterpret_cast<const std::uint32_t *>(
                g_state.rdram + processed_held_physical);
        static std::uint16_t last_raw_buttons = 0xFFFFU;
        static std::int8_t last_raw_x = -128;
        static std::int8_t last_raw_y = -128;
        static std::uint32_t last_processed_held = 0xFFFFFFFFU;
        if (raw_buttons != last_raw_buttons || raw_x != last_raw_x ||
                raw_y != last_raw_y || processed_held != last_processed_held) {
            char detail[192]{};
            std::snprintf(detail, sizeof(detail),
                    "raw_pad=0x80108EA0 buttons=0x%04X stick=(%d,%d) "
                    "processed_held_0x80109340=0x%08X vi=%llu",
                    raw_buttons, static_cast<int>(raw_x), static_cast<int>(raw_y),
                    processed_held, static_cast<unsigned long long>(count));
            trace("rw077-guest-input-state", detail);
            std::fprintf(stderr, "RW077_GUEST_INPUT_STATE %s\n", detail);
            std::fflush(stderr);
            last_raw_buttons = raw_buttons;
            last_raw_x = raw_x;
            last_raw_y = raw_y;
            last_processed_held = processed_held;
        }
    }
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw023_native_trace_enabled()) {
        std::fprintf(stderr, "RW023_VI_BOUNDARY vi=%llu graphics_task_count=%llu\n",
                static_cast<unsigned long long>(count),
                static_cast<unsigned long long>(g_state.graphics_task_count.load()));
    }
#endif
#endif
    if (g_options.stop_checkpoint == "first-vi") {
        g_state.checkpoint_reached = true;
        request_success("bounded VI checkpoint reached");
        return;
    }
    const auto tasks = g_state.graphics_task_count.load();
    if (g_options.stop_checkpoint == "first-graphics-task" && tasks >= 1) {
        g_state.checkpoint_reached = true;
        request_success("first headless graphics task completed its renderer callback");
        return;
    }
    if (g_options.stop_checkpoint == "multiple-graphics-tasks" &&
            tasks >= 2 && g_state.sp_dp_progress_observed && count >= 2) {
        g_state.checkpoint_reached = true;
        request_success("multiple graphics tasks prove SP/DP scheduler progress");
        return;
    }
    if (g_options.max_vi != 0 && count >= g_options.max_vi) {
        rw019_final_scheduler_snapshot("max_vi");
        fail("max_vi_reached", "requested Gate 6 checkpoint was not reached before the VI bound");
    }
}

void on_thread_create(std::uint8_t *, recomp_context *ctx) {
    ++g_state.thread_create_count;
    const auto entry = static_cast<std::uint32_t>(recomp::get_current_thread_entry_address());
    const auto stack = static_cast<std::uint32_t>(ctx->r29);
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw023_native_trace_enabled()) {
        std::fprintf(stderr,
                "RW023_THREAD_CREATE vi=%llu entry=0x%08X sp=0x%08X arg=0x%08X\n",
                static_cast<unsigned long long>(g_state.vi_count.load()), entry, stack,
                static_cast<unsigned>(ctx->r4));
    }
#endif
    ctx->mips3_float_mode = true;
    ctx->f_odd = &ctx->f1.u32l;
    trace("guest-thread-created", "entry=" + hex32(entry) + " sp=" + hex32(stack) +
            " arg=" + hex32(static_cast<std::uint32_t>(ctx->r4)));
}

void on_runtime_init(std::uint8_t *rdram, recomp_context *ctx) {
    ctx->mips3_float_mode = true;
    ctx->f_odd = &ctx->f1.u32l;
    rage_wars::asset_loader().bind_cart_rom(recomp::get_rom());
    g_state.runtime_initialized = true;
    g_state.rdram_allocated = rdram != nullptr;
    g_state.rdram = rdram;
    recomp_context mem_size_context{};
    osGetMemSize_recomp(rdram, &mem_size_context);
    constexpr std::int32_t os_mem_size = static_cast<std::int32_t>(0x80000318U);
    const auto api_size = static_cast<std::uint32_t>(mem_size_context.r2);
    const auto boot_size = static_cast<std::uint32_t>(MEM_W(os_mem_size, 0));
    const auto host_size = static_cast<std::uint32_t>(xr64::rage_wars::memory_profile::kRdramSize);
    std::fprintf(stderr,
            "RW_G2_MEMORY_PROFILE host_rdram=%u osGetMemSize=%u boot_osMemSize=%u "
            "kseg0_end=0x%08X kseg1_end=0x%08X expansion_pak=%s\n",
            host_size, api_size, boot_size,
            xr64::rage_wars::memory_profile::kKseg0End,
            xr64::rage_wars::memory_profile::kKseg1End,
            host_size > 0x00400000U ? "on" : "off");
    std::fflush(stderr);
    const auto peripheral = connected_device(0);
    std::uint16_t buttons = 0xFFFF;
    float stick_x = 1.0F;
    float stick_y = 1.0F;
    const bool input_available = get_input(0, &buttons, &stick_x, &stick_y);
    const char *input_mode = g_input_replay.enabled()
            ? "diagnostic-replay" : "physical-sdl";
    std::fprintf(stderr,
            "RW_G2_PERIPHERAL_PROFILE controller1=%s type=0x0005 input=%s pak=%s pak_size=%zu\n",
            peripheral.connected_device == ultramodern::input::Device::Controller ? "connected" : "disconnected",
            input_available ? input_mode : "unavailable",
            peripheral.connected_pak == ultramodern::input::Pak::ControllerPak ? "controller_pak" : "none",
            g_controller_pak_loaded ? g_controller_pak.size() : 0);
    std::fflush(stderr);
    trace("peripheral-profile", std::string("controller1=") +
            (peripheral.connected_device == ultramodern::input::Device::Controller ? "connected" : "disconnected") +
            " type=0x0005 input=" + (input_available ? input_mode : "unavailable") +
            " pak=" + (peripheral.connected_pak == ultramodern::input::Pak::ControllerPak ? "controller_pak" : "none") +
            " pak_size=" + std::to_string(g_controller_pak_loaded ? g_controller_pak.size() : 0));
    if (rdram == nullptr || api_size != host_size || boot_size != host_size) {
        fail("memory_profile_mismatch", "native RDRAM/API/IPL3 state does not match the configured profile");
        throw ultramodern::thread_terminated{};
    }
    load_overlays(0x00001000U, static_cast<std::int32_t>(0x00200400U), 0x00100000U);
    trace("initial-low-alias-activated", "ROM 0x1000..0x101000 mapped at guest 0x00200400 (boot-copy offset 0xC00)");
    rage_wars::register_native_resident_callable(
            static_cast<std::int32_t>(kMainHandoff), rage_wars_gate5_main_handoff);
    rage_wars::register_native_resident_callable(
            static_cast<std::int32_t>(kIdleThreadEntry), gate13_thread_entry_002933F8);
    rage_wars::register_native_resident_callable(
            static_cast<std::int32_t>(kMainThreadEntry), resident_wave14_func_00293420);
    trace("runtime-initialized", "model C main handoff and guest thread entries registered after overlay reset");
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    g_rw017_guest_execution_started = true;
#endif
}

HWND create_supervision_window() {
    const wchar_t *class_name = L"XR64RageWarsGate6";
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND window, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        if (message == WM_CLOSE) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = class_name;
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, class_name, L"XR64 Rage Wars",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 360,
            nullptr, nullptr, wc.hInstance, nullptr);
    // This HWND is an N64ModernRuntime supervision handle, not the game
    // surface. Keep it hidden; the SDL renderer owns the only visible window.

    return window;
}

} // namespace

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
extern "C" void xr64_rw017_trace_guest_pc(std::uint32_t pc, std::uint32_t ra) {
    if (!g_options.rw017_peripheral_trace) return;
    trace_rw017_peripheral(
            "layer=guest-pc operation=pc-observation guest_pc=" + hex32(pc) +
            " ra=" + hex32(ra));
}

extern "C" void xr64_rw022_allocation_return(
        std::uint8_t *rdram, recomp_context *ctx, std::uint32_t item, std::uint32_t caller_ra) {
    if (std::getenv("XR64_RW022_NATIVE_ALLOC_TRACE") == nullptr || item != 0x80100020U) return;
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    const auto runtime_thread = static_cast<std::uint32_t>(ultramodern::this_thread());
    const auto entry = static_cast<std::uint32_t>(recomp::get_current_thread_entry_address());
#else
    const std::uint32_t runtime_thread = 0;
    const std::uint32_t entry = 0;
#endif
    const auto word0 = static_cast<unsigned>(*reinterpret_cast<const std::int32_t *>(rdram + ((item + 0U) & 0x1FFFFFFFU)));
    const auto word1 = static_cast<unsigned>(*reinterpret_cast<const std::int32_t *>(rdram + ((item + 4U) & 0x1FFFFFFFU)));
    const auto word2 = static_cast<unsigned>(*reinterpret_cast<const std::int32_t *>(rdram + ((item + 8U) & 0x1FFFFFFFU)));
    const auto word3 = static_cast<unsigned>(*reinterpret_cast<const std::int32_t *>(rdram + ((item + 12U) & 0x1FFFFFFFU)));
    void *frames[6]{};
    const auto frame_count = CaptureStackBackTrace(0, 6, frames, nullptr);
    const auto module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(stderr,
            "RW022_ALLOC_RETURN item=0x%08X current_thread=0x%08X entry=0x%08X caller_ra=0x%08X "
            "sp=0x%08X ra=0x%08X old_word0=0x%08X old_word1=0x%08X old_word2=0x%08X old_word3=0x%08X\n",
            item, runtime_thread, entry, caller_ra, static_cast<unsigned>(ctx->r29), static_cast<unsigned>(ctx->r31),
            word0, word1, word2, word3);
    std::fprintf(stderr, "RW022_ALLOC_STACK frame_count=%hu", frame_count);
    for (USHORT index = 0; index < frame_count; ++index) {
        const auto rva = reinterpret_cast<std::uintptr_t>(frames[index]) - module_base;
        std::fprintf(stderr, " rva%hu=0x%llX", index, static_cast<unsigned long long>(rva));
    }
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

extern "C" void xr64_rw022_producer_call(std::uint8_t *, recomp_context *ctx, std::uint32_t pc) {
    if (std::getenv("XR64_RW022_NATIVE_ALLOC_TRACE") == nullptr) return;
    std::fprintf(stderr, "RW022_PRODUCER_CALL pc=0x%08X current_thread=0x%08X entry=0x%08X a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X sp=0x%08X\n", pc, static_cast<unsigned>(ultramodern::this_thread()), static_cast<unsigned>(recomp::get_current_thread_entry_address()), static_cast<unsigned>(ctx->r4), static_cast<unsigned>(ctx->r5), static_cast<unsigned>(ctx->r6), static_cast<unsigned>(ctx->r7), static_cast<unsigned>(ctx->r29));
    std::fflush(stderr);
}

extern "C" void xr64_rw022_item_submit(std::uint8_t *, recomp_context *ctx, std::uint32_t item, std::uint32_t selector, std::uint32_t remaining, std::uint32_t word0, std::uint32_t word1, std::uint32_t word2, std::uint32_t word3) {
    if (std::getenv("XR64_RW022_NATIVE_ALLOC_TRACE") == nullptr || item != 0x80100020U) return;
    std::fprintf(stderr, "RW022_ITEM_SUBMIT producer_function=0x002563F4 selector_at_0x00256434=0x%08X remaining=0x%08X current_thread=0x%08X entry=0x%08X ra=0x%08X sp=0x%08X word0=0x%08X word1=0x%08X word2=0x%08X word3=0x%08X\n", selector, remaining, static_cast<unsigned>(ultramodern::this_thread()), static_cast<unsigned>(recomp::get_current_thread_entry_address()), static_cast<unsigned>(ctx->r31), static_cast<unsigned>(ctx->r29), word0, word1, word2, word3);
    std::fflush(stderr);
}

extern "C" void xr64_rw023_producer_entry(
        std::uint8_t *rdram, recomp_context *ctx, std::uint32_t target) {
    if (!rw023_native_trace_enabled()) return;
    const auto read_word = [rdram](std::uint32_t address) {
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t *>(
                rdram + (address & xr64::rage_wars::memory_profile::kPhysicalMask)));
    };
    const auto sp = static_cast<std::uint32_t>(ctx->r29);
    std::fprintf(stderr,
            "RW023_PRODUCER_ENTRY target=0x%08X vi=%llu current_thread=0x%08X entry=0x%08X "
            "ra=0x%08X sp=0x%08X a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X "
            "stack10=0x%08X stack14=0x%08X stack18=0x%08X stack1c=0x%08X "
            "work_item=0x80100020 word0=0x%08X word1=0x%08X word2=0x%08X word3=0x%08X\n",
            target, static_cast<unsigned long long>(g_state.vi_count.load()),
            static_cast<unsigned>(ultramodern::this_thread()),
            static_cast<unsigned>(recomp::get_current_thread_entry_address()),
            static_cast<unsigned>(ctx->r31), sp,
            static_cast<unsigned>(ctx->r4), static_cast<unsigned>(ctx->r5),
            static_cast<unsigned>(ctx->r6), static_cast<unsigned>(ctx->r7),
            read_word(sp + 0x10U), read_word(sp + 0x14U),
            read_word(sp + 0x18U), read_word(sp + 0x1CU),
            read_word(0x80100020U), read_word(0x80100024U),
            read_word(0x80100028U), read_word(0x8010002CU));
    std::fflush(stderr);
}

extern "C" int xr64_rw023_force_ares_producer_path(
        std::uint8_t *rdram, recomp_context *ctx) {
    if (!g_options.rw023_force_ares_producer_path) return 0;
    const auto read_word = [rdram](std::uint32_t address) {
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t *>(
                rdram + (address & xr64::rage_wars::memory_profile::kPhysicalMask)));
    };
    const auto current_thread = static_cast<std::uint32_t>(ultramodern::this_thread());
    const auto entry = static_cast<std::uint32_t>(recomp::get_current_thread_entry_address());
    const bool exact_context = current_thread == 0x801197C0U && entry == 0x00293420U &&
            static_cast<std::uint32_t>(ctx->r4) == 0x0028B244U &&
            static_cast<std::uint32_t>(ctx->r5) == 0x801820C0U &&
            static_cast<std::uint32_t>(ctx->r6) == 0x00000020U &&
            static_cast<std::uint32_t>(ctx->r7) == 0x800EDAD8U;
    const bool clean_item = read_word(0x80100020U) == 0 && read_word(0x80100024U) == 0 &&
            read_word(0x80100028U) == 0 && read_word(0x8010002CU) == 0;
    unsigned expected = 0;
    const bool one_shot = exact_context && clean_item &&
            g_rw023_forced_hook_count.compare_exchange_strong(expected, 1U);
    if (!one_shot) {
        std::fprintf(stderr,
                "RW023_FORCE_HOOK armed=1 fired=0 reason=%s current_thread=0x%08X entry=0x%08X "
                "a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X count=%u\n",
                !exact_context ? "context_mismatch" : (!clean_item ? "work_item_not_clean" : "already_fired"),
                current_thread, entry, static_cast<unsigned>(ctx->r4),
                static_cast<unsigned>(ctx->r5), static_cast<unsigned>(ctx->r6),
                static_cast<unsigned>(ctx->r7), g_rw023_forced_hook_count.load());
        std::fflush(stderr);
        return 0;
    }
    const auto sp = static_cast<std::uint32_t>(ctx->r29);
    std::fprintf(stderr,
            "RW023_FORCE_HOOK armed=1 fired=1 count=1 original_target=0x002563F4 "
            "forced_target=0x0025631C work_item=0x80100020 current_thread=0x%08X entry=0x%08X "
            "ra=0x%08X sp=0x%08X a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X "
            "stack10=0x%08X stack14=0x%08X stack18=0x%08X stack1c=0x%08X\n",
            current_thread, entry, static_cast<unsigned>(ctx->r31), sp,
            static_cast<unsigned>(ctx->r4), static_cast<unsigned>(ctx->r5),
            static_cast<unsigned>(ctx->r6), static_cast<unsigned>(ctx->r7),
            read_word(sp + 0x10U), read_word(sp + 0x14U),
            read_word(sp + 0x18U), read_word(sp + 0x1CU));
    std::fflush(stderr);
    return 1;
}

extern "C" void xr64_rw023_forced_return(std::uint8_t *rdram, recomp_context *) {
    const auto read_word = [rdram](std::uint32_t address) {
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t *>(
                rdram + (address & xr64::rage_wars::memory_profile::kPhysicalMask)));
    };
    std::fprintf(stderr,
            "RW023_FORCE_RETURN count=%u work_item=0x80100020 word0=0x%08X word1=0x%08X word2=0x%08X word3=0x%08X\n",
            g_rw023_forced_hook_count.load(), read_word(0x80100020U), read_word(0x80100024U),
            read_word(0x80100028U), read_word(0x8010002CU));
    std::fflush(stderr);
}
#endif
void rage_wars_gate5_main_handoff(unsigned char *rdram, recomp_context *ctx) {
    g_state.generated_entrypoint_invoked = true;
    const auto handoff_count = g_state.resident_main_handoff_count.fetch_add(1) + 1;
    std::ostringstream detail;
    detail << "reached 0x002A70F0 handoff count=" << handoff_count
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()));
    trace("generated-entrypoint-invoked", detail.str());
    trace("rw076-handoff-context", "ra=" + hex32(static_cast<std::uint32_t>(ctx->r31)) + " sp=" + hex32(static_cast<std::uint32_t>(ctx->r29)) + " a0=" + hex32(static_cast<std::uint32_t>(ctx->r4)) + " a1=" + hex32(static_cast<std::uint32_t>(ctx->r5)) + " a2=" + hex32(static_cast<std::uint32_t>(ctx->r6)) + " a3=" + hex32(static_cast<std::uint32_t>(ctx->r7)));
    if (handoff_count > 1) {
        trace("resident-main-repeat-ignored", "duplicate native main handoff skipped rw_main");
        return;
    }
    if (g_options.stop_checkpoint == "main-handoff") {
        g_state.checkpoint_reached = true;
        request_success("generated recomp_entrypoint reached the native main handoff");
        throw ultramodern::thread_terminated{};
    }
    trace("resident-main-entered", "continuing directly into generated rw_main");
    rw_main(rdram, ctx);
    trace("resident-main-returned", "initial guest thread exited after creating runtime workers");
    throw ultramodern::thread_terminated{};
}

int main(int argc, char **argv) {
    std::string argument_error;
#ifdef XR64_DEMO_BUILD
    const HANDLE demo_instance=CreateMutexW(nullptr,FALSE,L"Local\\XR64Studios.RageWars");
    if(!demo_instance || GetLastError()==ERROR_ALREADY_EXISTS) {
        xr64::rage_wars::demo::show_error("Rage Wars is already running, or its instance lock could not be created.");
        if(demo_instance)CloseHandle(demo_instance);
        return 1;
    }
    int startup_argc=0;
    wchar_t** startup_argv=CommandLineToArgvW(GetCommandLineW(),&startup_argc);
    if(!startup_argv) {
        xr64::rage_wars::demo::show_error("Windows could not read the launch arguments.");
        CloseHandle(demo_instance);
        return 2;
    }
    xr64::rage_wars::demo::Startup startup;
    const auto startup_outcome=xr64::rage_wars::demo::resolve_startup(
            startup_argc,startup_argv,startup,argument_error);
    LocalFree(startup_argv);
    if(startup_outcome==xr64::rage_wars::demo::StartupOutcome::Help) {
        std::fwrite(xr64::rage_wars::demo::startup_usage().data(),1,
                xr64::rage_wars::demo::startup_usage().size(),stdout);
        CloseHandle(demo_instance);
        return 0;
    }
    if(startup_outcome==xr64::rage_wars::demo::StartupOutcome::SetupCancelled) {
        CloseHandle(demo_instance);
        return 0;
    }
    if(startup_outcome!=xr64::rage_wars::demo::StartupOutcome::Ready) {
        std::fprintf(stderr,"rage_wars: %s\n",argument_error.c_str());
        if(!startup.headless)xr64::rage_wars::demo::show_error(argument_error.c_str());
        CloseHandle(demo_instance);
        return 2;
    }
    g_options.rom=startup.rom;
    g_options.data=startup.data;
    g_options.controller_pak=startup.controller_pak;
    g_options.trace=startup.trace;
    g_options.summary=startup.summary;
    g_options.input_replay=startup.input_replay;
    g_options.stop_checkpoint=startup.stop_checkpoint;
    g_options.max_vi=startup.max_vi;
    g_options.max_seconds=startup.max_seconds;
    g_options.headless=startup.headless;
    g_options.visible=startup.visible;
    g_options.mute=startup.mute;
    g_options.developer=startup.developer;
    g_options.controller_no_pak=startup.no_pak;
    g_options.skip_controller_pak_selftest=startup.skip_pak_selftest;
    g_options.godot_live_bridge=startup.godot_live_bridge;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    g_options.rw017_peripheral_trace=startup.rw017_peripheral_trace;
    g_options.rw023_force_ares_producer_path=startup.rw023_force_ares_producer_path;
#endif
    _wputenv_s(L"XR64_PORT_OPTIONS_CONFIG",(startup.data/L"port-options.json").c_str());
    _wputenv_s(L"XR64_XR_PRESENTATION",L"independent");
    if(!startup.weapon_assets.empty())_wputenv_s(L"XR64_WEAPON_ASSETS",startup.weapon_assets.c_str());
    if(startup.weapon_calibration) {
        const bool ok=xr64::rage_wars::recomp::run_gate5_weapon_calibration(argument_error);
        if(!ok)xr64::rage_wars::demo::show_error(argument_error.c_str());
        CloseHandle(demo_instance);
        return ok?0:1; // No guest boot, opponents, Pak access or game simulation.
    }
    bool attempt_xr=startup.vr_preference==xr64::rage_wars::recomp::XrStartupPreference::On;
    if(startup.vr_preference==xr64::rage_wars::recomp::XrStartupPreference::Auto) {
        std::string readiness_reason;
        attempt_xr=xr64::rage_wars::recomp::gate5_xr_auto_ready_hint(readiness_reason);
        if(!attempt_xr && !readiness_reason.empty())
            std::fprintf(stderr,"rage_wars: Auto startup selected PC mode (%s)\n",readiness_reason.c_str());
    }
    g_options.attempt_xr=attempt_xr;
#else
    AddVectoredExceptionHandler(1, gate6_fault_locator);
    if (!parse_args(argc, argv, argument_error)) {
        std::fprintf(stderr, "rage_wars_pc: %s\n", argument_error.c_str());
#if defined(XR64_RAGE_WARS_CLEAN_RUN)
        std::fprintf(stderr, "usage: rage_wars_pc.exe --rom <path> [--controller-pak <32KiB-working-copy>] [--controller-no-pak] [--skip-controller-pak-selftest] [--godot-live-bridge] [--headless|--visible] [--trace <path>] [--summary <path>] [--max-vi <n>] [--max-seconds <n>] [--input-replay <path>] [--stop-at-checkpoint <none|main-handoff|first-vi|first-graphics-task|multiple-graphics-tasks>] [--mute] [--developer]\n");
#else
        std::fprintf(stderr, "usage: rage_wars_pc.exe --rom <path> [--controller-pak <32KiB-working-copy>] [--controller-no-pak] [--skip-controller-pak-selftest] [--godot-live-bridge] [--rw017-peripheral-trace] [--rw023-force-ares-producer-path] [--headless|--visible] [--trace <path>] [--summary <path>] [--max-vi <n>] [--max-seconds <n>] [--input-replay <path>] [--stop-at-checkpoint <none|main-handoff|first-vi|first-graphics-task|multiple-graphics-tasks>] [--mute] [--developer]\n");
#endif
        return 2;
    }
#endif
    const char *inherited_replay = std::getenv("XR64_RW_INPUT_REPLAY");
    g_inherited_input_replay_cleared =
            inherited_replay != nullptr && inherited_replay[0] != '\0';
    _putenv_s("XR64_RW_INPUT_REPLAY", "");
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    _putenv_s("XR64_RW017_TRACE", g_options.rw017_peripheral_trace ? "1" : "");
#endif
    if (!load_controller_pak(argument_error)) {
#ifdef XR64_DEMO_BUILD
        if(!g_options.headless)xr64::rage_wars::demo::show_error(argument_error.c_str());
#endif
        std::fprintf(stderr, "rage_wars_pc: %s\n", argument_error.c_str());
        return 2;
    }
    g_controller_present = g_options.controller_no_pak || g_controller_pak_loaded;
#ifndef XR64_DEMO_BUILD
    if (!g_options.trace.empty()) {
        std::filesystem::create_directories(g_options.trace.parent_path());
        g_trace.open(g_options.trace, std::ios::binary | std::ios::trunc);
    }
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
    g_input_replay.set_trace_callback(trace_input_replay_diagnostic);
#else
    g_input_replay.set_trace_callback(trace_input_replay);
#endif
    if (g_options.input_replay.empty()) {
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
        rw_replay_diag_emit("RW076_REPLAY_CONFIG enabled=0 events=0");
#endif
        trace("rw077-input-mode",
                std::string("physical-sdl inherited-replay-cleared=") +
                (g_inherited_input_replay_cleared ? "yes" : "no"));
    } else if (!g_input_replay.load_file(
            g_options.input_replay.string(), argument_error)) {
        std::fprintf(stderr, "rage_wars_pc: %s", argument_error.c_str());
        return 2;
    } else {
        trace("rw077-input-mode", "diagnostic-replay live-input=disabled");
        trace("rw076-input-replay-loaded",
                "events=" + std::to_string(g_input_replay.event_count()) +
                " anchor=controller-startup-confirmation");
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
        {
            std::ostringstream line;
            line << "RW076_REPLAY_CONFIG enabled=" << g_input_replay.enabled()
                 << " events=" << g_input_replay.event_count()
                 << " controller_present=" << g_controller_present
                 << " pak_loaded=" << g_controller_pak_loaded;
            rw_replay_diag_emit(line.str());
        }
#endif
    }
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    ultramodern::set_runtime_trace_callback(
            g_options.rw017_peripheral_trace ? runtime_order_trace : nullptr);
#endif
    trace("host-start");
#endif

#ifdef XR64_DEMO_BUILD
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
    g_input_replay.set_trace_callback(trace_input_replay_diagnostic);
#endif
    if (!g_options.input_replay.empty() &&
            !g_input_replay.load_file(g_options.input_replay.string(),argument_error)) {
        if(!g_options.headless)xr64::rage_wars::demo::show_error(argument_error.c_str());
        return 2;
    }
#ifdef XR64_RW_REPLAY_DIAGNOSTICS
    {
        std::ostringstream line;
        line << "RW076_REPLAY_CONFIG enabled=" << g_input_replay.enabled()
             << " events=" << g_input_replay.event_count()
             << " controller_present=" << g_controller_present
             << " pak_loaded=" << g_controller_pak_loaded;
        rw_replay_diag_emit(line.str());
    }
#endif
    const auto state_dir=g_options.data/L"runtime";
#else
    const auto state_dir = (g_options.summary.empty()
            ? std::filesystem::current_path() / "rage_wars_gate5_state"
            : g_options.summary.parent_path() / "rage_wars_gate5_state");
#endif
    std::filesystem::create_directories(state_dir);
    xr64::rage_wars::crash::initialize(state_dir.parent_path() / L"crashes", g_options.visible);
    recomp::overlays::set_callable_failure_handler(xr64::rage_wars::crash::missing_callable);
    recomp::register_config_path(state_dir);
    rage_wars_register_generated_overlays();
    rage_wars::install_asset_loader_service();
    rage_wars::install_code_residency_service();
    rage_wars::install_scheduler_service();

    recomp::GameEntry game{
        .rom_hash = kRomXxh3,
        .internal_name = "Turok: Rage Wars    ",
        .display_name = "Turok: Rage Wars (US v1.0)",
        .game_id = std::u8string(kGameId),
        .mod_game_id = "xr64_rage_wars",
        .save_type = recomp::SaveType::None,
        .thumbnail_bytes = {},
        .is_enabled = true,
        .decompression_routine = nullptr,
        .has_compressed_code = false,
        .entrypoint_address = static_cast<gpr>(static_cast<std::int32_t>(kEntrypoint)),
        .entrypoint = recomp_entrypoint,
        .thread_create_callback = on_thread_create,
        .on_init_callback = on_runtime_init,
    };
    if (!recomp::register_game(game)) {
        {
            std::lock_guard lock(g_state.mutex);
            g_state.status = "failed";
            g_state.failure_code = "game_registration_failed";
            g_state.detail = "N64ModernRuntime rejected the GameEntry";
        }
        write_summary();
        return 3;
    }
    std::u8string selected_id(kGameId);
#ifdef XR64_DEMO_BUILD
    const auto validation = recomp::select_rom_in_memory(g_options.rom, selected_id);
#else
    const auto validation = recomp::select_rom(g_options.rom, selected_id);
#endif
    if (validation != recomp::RomValidationError::Good) {
        {
            std::lock_guard lock(g_state.mutex);
            g_state.status = "failed";
            g_state.failure_code = "rom_validation_failed";
            g_state.detail = "N64ModernRuntime validation code " + std::to_string(static_cast<int>(validation));
        }
        write_summary();
        return 4;
    }
    trace("rom-validated", "XXH3 and internal name accepted");

    const HWND window = create_supervision_window();
    if (!window) {
        {
            std::lock_guard lock(g_state.mutex);
            g_state.status = "failed";
            g_state.failure_code = "window_creation_failed";
            g_state.detail = "Win32 supervision window creation failed";
        }
        write_summary();
        return 5;
    }

    xr64::rage_wars::audio::initialize();
    recomp::Configuration cfg{};
#ifdef XR64_DEMO_BUILD
    cfg.project_version = {0, 1, 0, "-xr64-demo"};
#else
    cfg.project_version = {0, 6, 0, "-xr64-gate6"};
#endif
    cfg.window_handle = {window, GetCurrentThreadId()};
    cfg.rsp_callbacks = {get_rsp_microcode};
    cfg.renderer_callbacks = {create_renderer};
    cfg.audio_callbacks = {queue_samples, frames_remaining, set_frequency};
    cfg.input_callbacks = {poll_input, get_input, set_rumble, connected_device,
            read_controller_pak, write_controller_pak};
    cfg.gfx_callbacks = {nullptr, nullptr, update_gfx};
    cfg.events_callbacks = {vi_callback, gfx_initialized};
    cfg.error_handling_callbacks = {message_box};
    cfg.threads_callbacks = {thread_name};
    cfg.threads_callbacks.thread_activated = rage_wars_thread_activated;
    ultramodern::input::set_callbacks(cfg.input_callbacks);
    rage_wars::controller_service().configure({
            controller_port_status,
            get_input,
            read_controller_pak,
            write_controller_pak,
            signal_controller_completion});
#ifndef XR64_DEMO_BUILD
    if (!g_options.skip_controller_pak_selftest && !validate_controller_pak_bridge()) {
        {
            std::lock_guard lock(g_state.mutex);
            g_state.status = "failed";
            g_state.failure_code = "controller_pak_selftest_failed";
            g_state.detail = "native Controller Pak bridge did not expose a coherent one-bank filesystem";
        }
        write_summary();
        if (IsWindow(window)) DestroyWindow(window);
        return 7;
    }
    if (g_options.skip_controller_pak_selftest) {
        std::fprintf(stderr, "RW_G2_PAK_SELFTEST skipped=yes reason=rw015_guest_isolation\n");
        std::fflush(stderr);
    }
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (g_options.rw017_peripheral_trace) {
        std::fprintf(stderr,
                "RW017_INSTRUMENTATION enabled=yes mode=observational source_attribution=guest_after_runtime_init max_vi=%llu\n",
                static_cast<unsigned long long>(g_options.max_vi));
        std::fflush(stderr);
    }
#endif

    std::jthread watchdog([seconds = g_options.max_seconds](std::stop_token token) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (!token.stop_requested() && !g_state.quit_requested) {
            if (seconds != 0 && std::chrono::steady_clock::now() >= deadline) {
                fail("max_seconds_reached", "runtime exceeded the bounded Gate 6 deadline");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

#endif
#ifdef XR64_DEMO_BUILD
    std::jthread consumer_watchdog;
    if (g_options.max_seconds != 0) {
        consumer_watchdog = std::jthread([seconds = g_options.max_seconds](std::stop_token token) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            while (!token.stop_requested() && !g_state.quit_requested) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    fail("max_seconds_reached", "runtime exceeded the requested diagnostic deadline");
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }
#endif
    recomp::start(cfg);
#ifdef XR64_DEMO_BUILD
    if (consumer_watchdog.joinable()) {
        consumer_watchdog.request_stop();
        consumer_watchdog.join();
    }
#else
    watchdog.request_stop();
    watchdog.join();
#endif
    xr64::rage_wars::audio::shutdown();
    if (IsWindow(window)) DestroyWindow(window);
    write_summary();
#ifdef XR64_DEMO_BUILD
    const UINT exit_code = g_state.failure_code.empty() ? 0U : 6U;
#else
    const UINT exit_code = g_state.failure_code.empty() && g_state.checkpoint_reached ? 0U : 6U;
#endif
    std::fflush(stdout);
    std::fflush(stderr);
    ExitProcess(exit_code);
}

namespace {
std::atomic<std::uint64_t> g_rw052_config_gate_entries{0};
std::atomic<std::uint64_t> g_rw052_pre_44afe0_entries{0};
std::atomic<std::uint64_t> g_rw052_after_44afe0_entries{0};
}

extern "C" void xr64_rw052_config_gate_transition(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::atomic<std::uint64_t> *counter = nullptr;
    std::string_view stage;
    switch (guest_pc) {
        case 0x00291530U:
            counter = &g_rw052_config_gate_entries;
            stage = "config-gate";
            break;
        case 0x00291538U:
            counter = &g_rw052_pre_44afe0_entries;
            stage = "pre-0044afe0";
            break;
        case 0x00291540U:
            counter = &g_rw052_after_44afe0_entries;
            stage = "after-0044afe0";
            break;
        default:
            return;
    }

    const auto invocation = counter->fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " v1=" << hex32(static_cast<std::uint32_t>(ctx->r3));
    trace("rw052-config-gate-transition", detail.str());
    std::fprintf(stderr, "RW052_CONFIG_GATE_TRANSITION %s\n", detail.str().c_str());
    std::fflush(stderr);
}

namespace {
std::atomic<std::uint64_t> g_rw053_entries[8]{};
}

extern "C" void xr64_rw053_initializer_frontier(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::size_t index = 0;
    std::string_view stage;
    switch (guest_pc) {
        case 0x0044AFE0U:
            index = 0; stage = "entry"; break;
        case 0x0044B00CU:
            index = 1; stage = "after-00285a14"; break;
        case 0x0044B034U:
            index = 2; stage = "after-002bab90-a"; break;
        case 0x0044B050U:
            index = 3; stage = "after-002bab90-b"; break;
        case 0x0044B060U:
            index = 4; stage = "after-002bb480"; break;
        case 0x0044B078U:
            index = 5; stage = "after-002b77b0"; break;
        case 0x0044B080U:
            index = 6; stage = "after-002bacb0"; break;
        case 0x0044B09CU:
            index = 7; stage = "after-002bb350"; break;
        default:
            return;
    }

    const auto invocation = g_rw053_entries[index].fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2))
           << " v1=" << hex32(static_cast<std::uint32_t>(ctx->r3));
    trace("rw053-initializer-frontier", detail.str());
    std::fprintf(stderr, "RW053_INITIALIZER_FRONTIER %s\n", detail.str().c_str());
    std::fflush(stderr);
}

namespace {
std::atomic<std::uint64_t> g_rw054_entries[3]{};
}

extern "C" void xr64_rw054_accessor_transition(
        std::uint8_t *, recomp_context *ctx, std::uint32_t guest_pc) {
    std::size_t index = 0;
    std::string_view stage;
    switch (guest_pc) {
        case 0x002BACB0U:
            index = 0; stage = "entry"; break;
        case 0x002BACC0U:
            index = 1; stage = "load-result"; break;
        case 0x002BACC4U:
            index = 2; stage = "return"; break;
        default:
            return;
    }

    const auto invocation = g_rw054_entries[index].fetch_add(1U) + 1U;
    if (invocation != 1U) return;

    std::ostringstream detail;
    detail << "stage=" << stage << " guest_pc=" << hex32(guest_pc)
           << " vi=" << g_state.vi_count.load()
           << " thread=" << hex32(static_cast<std::uint32_t>(ultramodern::this_thread()))
           << " entry=" << hex32(static_cast<std::uint32_t>(recomp::get_current_thread_entry_address()))
           << " a0=" << hex32(static_cast<std::uint32_t>(ctx->r4))
           << " v0=" << hex32(static_cast<std::uint32_t>(ctx->r2));
    trace("rw054-accessor-transition", detail.str());
    std::fprintf(stderr, "RW054_ACCESSOR_TRANSITION %s\n", detail.str().c_str());
    std::fflush(stderr);
}
