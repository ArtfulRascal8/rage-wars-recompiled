#include "rage_wars_crash_report.hpp"
#include "rage_wars_audio.hpp"
#include "rage_wars_guest_clock.hpp"
#include "rage_wars_audio_telemetry.hpp"
#include "librecomp/rsp.hpp"
#include "recomp.h"
#include <ultramodern/ultramodern.hpp>
#include <rage_wars_controller_service.hpp>
#include <librecomp/game.hpp>
#include <librecomp/overlays.hpp>
#include "funcs.h"
#include "librecomp/sections.h"
#include "recomp_runtime.hpp"
#include "runtime_boundary.hpp"
#include "scripted_input.hpp"
#include "rage_wars_asset_loader.hpp"
#include "pcrage_recovery_asset.h"
#include "gate5_runtime.hpp"
#include "rage_wars_code_residency.hpp"
#include "rage_wars_desktop_renderer.hpp"
#include "rage_wars_type1d.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csetjmp>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#if defined(_WIN32)
#include <intrin.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <vector>

#if defined(XR64_RAGE_WARS_USE_N64MODERN_VI_ADAPTERS)
extern "C" void osCreateViManager_recomp(std::uint8_t *, recomp_context *);
extern "C" void osViSetEvent_recomp(std::uint8_t *, recomp_context *);
#endif
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
extern "C" {
void osInitialize_recomp(std::uint8_t *, recomp_context *);
void __osDisableInt_recomp(std::uint8_t *, recomp_context *);
void __osRestoreInt_recomp(std::uint8_t *, recomp_context *);
void osGetThreadPri_recomp(std::uint8_t *, recomp_context *);
void osCreateThread_recomp(std::uint8_t *, recomp_context *);
void osStartThread_recomp(std::uint8_t *, recomp_context *);
void osSetThreadPri_recomp(std::uint8_t *, recomp_context *);
void osCreateMesgQueue_recomp(std::uint8_t *, recomp_context *);
void osRecvMesg_recomp(std::uint8_t *, recomp_context *);
void osSendMesg_recomp(std::uint8_t *, recomp_context *);
void osJamMesg_recomp(std::uint8_t *, recomp_context *);
void osSetEventMesg_recomp(std::uint8_t *, recomp_context *);
}
#endif
extern "C" void resident_wave9_func_002BAB90(std::uint8_t *, recomp_context *);
extern "C" void resident_wave11_func_002BB480(std::uint8_t *, recomp_context *);
extern "C" void trace_func_002BB7E0_r000BC3E0(std::uint8_t *, recomp_context *);

#include "recomp_overlays.inl"
#include "rdram_profile.hpp"

namespace {

constexpr std::size_t kSectionTableEntryCount = sizeof(section_table) / sizeof(section_table[0]);

constexpr std::size_t kRdramSize = xr64::rage_wars::memory_profile::kRdramSize;
constexpr std::size_t kRomSize = 8U * 1024U * 1024U;
constexpr std::size_t kInitialDmaSize = 1U * 1024U * 1024U;
constexpr std::size_t kInitialRomOffset = 0x1000U;
constexpr std::size_t kEntrypointPhysical = 0x400U;
constexpr std::string_view kInternalName = "Turok: Rage Wars    ";
constexpr std::uint32_t kMainAddress = 0x002A70F0U;
constexpr std::uint32_t kEntrypointBreak = 0x80000484U;
constexpr std::uint32_t kType1dRuntimeAddress = 0x800DE17CU;

std::uint32_t g_missing_lookup = 0;
bool g_intercept_main = false;
bool g_main_boundary_reached = false;
bool g_entrypoint_break_reached = false;
bool g_resident_running = false;
std::jmp_buf g_resident_boundary_jump;
xr64::rage_wars::gate12::RuntimeState g_runtime;
xr64::rage_wars::recomp::RageWarsDesktopRenderer g_desktop_renderer;

struct HostThreadState {
    std::uint32_t thread = 0;
    std::uint32_t entry = 0;
    std::uint32_t argument = 0;
    std::uint32_t stack = 0;
    bool started = false;
};

struct HostVmRegion {
    std::uint32_t base = 0;
    std::uint32_t source = 0;
    std::vector<std::uint8_t> bytes;
};

std::mutex g_vm_mutex;
std::vector<HostVmRegion> g_vm_regions;
std::uint32_t g_vm_next_page = 0x800U;

std::mutex g_host_mutex;
std::condition_variable g_host_cv;
std::unordered_map<std::uint32_t, std::shared_ptr<HostThreadState>> g_host_threads;
std::vector<std::thread> g_host_workers;
std::string g_async_error;
thread_local std::uint32_t g_host_thread = 0;
thread_local std::uint32_t g_host_entry = 0;
thread_local std::uint32_t g_host_argument = 0;
thread_local recomp_context *g_host_context = nullptr;
thread_local bool g_in_tlb_fault = false;
const std::vector<std::uint8_t> *g_rom = nullptr;
bool g_host_stop = false;
std::uint64_t g_host_threads_created = 0;
std::uint64_t g_host_threads_started = 0;
std::uint64_t g_host_queue_receives = 0;
std::uint64_t g_host_queue_sends = 0;
std::uint64_t g_main_frame_wait_calls = 0;
std::uint64_t g_synthetic_retraces = 0;
std::uint64_t g_synthetic_retraces_consumed = 0;
std::uint64_t g_graphics_task_number = 0;
std::uint64_t g_graphics_task_generations = 0;
std::uint8_t *g_si_event_rdram = nullptr;
std::uint32_t g_si_event_queue = 0;
std::uint32_t g_si_event_message = 0;

void deliver_fresh_si_message();

// Donor platform contract: the native controller service answers the
// status/input commands that precede each SI completion.  This checkpoint
// only requires the proven connected-controller/neutral-input portion.
bool fresh_controller_port_status(
        int controller, ::rage_wars::ControllerPortStatus *status) {
    if (controller != 0 || status == nullptr) return false;
    status->connected = true;
    status->controller_pak = false;
    status->type = 0x0005;
    return true;
}

bool fresh_controller_input(
        int controller, std::uint16_t *buttons, float *x, float *y) {
    if (controller != 0 || buttons == nullptr || x == nullptr || y == nullptr) {
        return false;
    }
    *buttons = 0;
    *x = 0.0F;
    *y = 0.0F;
    return true;
}

// Donor contract: the SI DMA service releases the guest's OS_EVENT_SI
// registration after each completed PIF transaction.  The fresh checkpoint
// does not use the donor executable's full input/Pak front end, but it must
// still install this completion edge before resident code can wait on the SI
// event queue.
void signal_controller_completion() {
    std::fprintf(stderr, "RW017_SI_COMPLETE source=controller-service\n");
    std::fflush(stderr);
    ultramodern::send_si_message();
    deliver_fresh_si_message();
}

class GuestTimerProducer {
public:
    void start(std::uint8_t *rdram) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable()) return;
        rdram_ = rdram;
        epoch_ = std::chrono::steady_clock::now();
        stopping_ = false;
        armed_ = false;
        worker_ = std::thread(&GuestTimerProducer::run, this);
        std::fprintf(stderr, "RW016_TIMER_OWNER start=guest-compare-service\n");
        std::fflush(stderr);
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            armed_ = false;
        }
        condition_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        rdram_ = nullptr;
        stopping_ = false;
    }

    void arm(std::uint32_t compare) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (rdram_ == nullptr) return;
            if (compare == 0) {
                armed_ = false;
            } else {
                const auto delay = std::chrono::microseconds{
                        static_cast<std::int64_t>(compare) * 1000 / 46875};
                deadline_ = epoch_ + delay;
                armed_ = true;
                std::fprintf(stderr, "RW016_TIMER_ARM compare=0x%08X\n", compare);
                std::fflush(stderr);
            }
        }
        condition_.notify_all();
    }

    std::uint64_t current_ticks() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (epoch_ == std::chrono::steady_clock::time_point{}) return 0;
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - epoch_).count();
        return xr64::rage_wars::guest_clock::ticks_from_microseconds(
                static_cast<std::uint64_t>(micros));
    }

    std::uint32_t current_count() {
        return static_cast<std::uint32_t>(current_ticks());
    }

private:
    void run() {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || armed_; });
            if (stopping_) return;
            const auto deadline = deadline_;
            const bool interrupted = condition_.wait_until(lock, deadline, [this, deadline] {
                return stopping_ || !armed_ || deadline_ != deadline;
            });
            if (stopping_) return;
            if (interrupted || !armed_) continue;
            armed_ = false;
            std::uint8_t *rdram = rdram_;
            lock.unlock();

            std::fprintf(stderr, "RW016_TIMER_FIRE handler=0x002BB7E0\n");
            std::fflush(stderr);
            recomp_context context{};
            context.r29 = static_cast<gpr>(static_cast<std::int32_t>(0x803FF000U));
            context.f_odd = &context.f1.u32l;
            context.mips3_float_mode = true;
            trace_func_002BB7E0_r000BC3E0(rdram, &context);
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::uint8_t *rdram_ = nullptr;
    std::chrono::steady_clock::time_point epoch_{};
    std::chrono::steady_clock::time_point deadline_{};
    bool armed_ = false;
    bool stopping_ = false;
};

GuestTimerProducer g_guest_timer_producer;

std::atomic<std::uint64_t> g_code_residency_requests = 0;
std::uint64_t g_code_page_materializations = 0;
std::mutex g_code_residency_mutex;
std::atomic<std::uint64_t> g_vi_ticks = 0;
std::atomic<std::uint64_t> g_vi_29a_enqueued = 0;
std::atomic<std::uint64_t> g_vi_29a_received = 0;
constexpr std::uint32_t kIdleThreadEntry = 0x002933F8U;
constexpr std::uint32_t kMainThreadEntry = 0x00293420U;
constexpr std::uint32_t kSyntheticRetraceMessage = 0x8000035CU;
constexpr std::uint32_t kOsScRetraceMsg = 1U;
constexpr std::uint64_t kCaptureRetraceBudget = 8U;

std::uint64_t capture_retrace_budget() {
    const char *value = std::getenv("XR64_RW022_RETRACE_BUDGET");
    if (value == nullptr || *value == '\0') return kCaptureRetraceBudget;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0 || parsed > 100000ULL) {
        return kCaptureRetraceBudget;
    }
    return static_cast<std::uint64_t>(parsed);
}

struct GraphicsTaskCapture {
    bool ready = false;
    std::uint32_t raw_task = 0;
    std::uint32_t wrapper = 0;
    std::array<std::uint8_t, 0x40> ostask{};
    std::vector<std::uint8_t> canonical_rdram;
};

struct GraphicsCompletionRecord {
    std::uint64_t task_number = 0;
    std::uint32_t task_address = 0;
    std::uint32_t display_list = 0;
    std::uint32_t producer_return = 0;
    std::uint64_t vi_retrace = 0;
    std::uint32_t message = 0;
};

std::deque<GraphicsCompletionRecord> g_graphics_completions;

GraphicsTaskCapture g_graphics_capture;
std::mutex g_graphics_capture_mutex;
thread_local std::uint32_t g_sp_raw_task = 0;

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error(message);
}

#if defined(_WIN32)
#include <DbgHelp.h>

LONG WINAPI gate14_exception_trace(EXCEPTION_POINTERS *info) {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto instruction = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const auto access = info->ExceptionRecord->NumberParameters >= 2
            ? static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[0])
            : 0ULL;
    const auto target = info->ExceptionRecord->NumberParameters >= 2
            ? reinterpret_cast<void *>(info->ExceptionRecord->ExceptionInformation[1]) : nullptr;
    std::fprintf(stderr,
            "G14 exception code=0x%08lX host_thread=0x%08X entry=0x%08X address=%p rva=0x%llX access=%llu target=%p rip=0x%llX rsp=0x%llX\n",
            info->ExceptionRecord->ExceptionCode, g_host_thread, g_host_entry,
            info->ExceptionRecord->ExceptionAddress,
            static_cast<unsigned long long>(instruction - module),
            access, target,
            static_cast<unsigned long long>(info->ContextRecord->Rip),
            static_cast<unsigned long long>(info->ContextRecord->Rsp));
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (unsigned index = 0; index < 16; ++index) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(),
                &frame, &context, nullptr, SymFunctionTableAccess64,
                SymGetModuleBase64, nullptr)) {
            break;
        }
        std::fprintf(stderr, "G14 exception_stack[%u]=0x%llX rva=0x%llX\n",
                index, static_cast<unsigned long long>(frame.AddrPC.Offset),
                static_cast<unsigned long long>(frame.AddrPC.Offset - module));
    }
    SymCleanup(process);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

gpr n64_address(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

std::filesystem::path resolve_type1d_data_path(const std::filesystem::path &rom_path,
        const std::filesystem::path &requested_path) {
    if (!requested_path.empty()) return requested_path;
    if (const char *environment_path = std::getenv("XR64_RAGE_WARS_TYPE1D_DATA");
            environment_path != nullptr && environment_path[0] != '\0') {
        return environment_path;
    }
    return rom_path.parent_path() / "rage_wars_type1d.bin";
}

void publish_type1d_data(std::uint8_t *rdram,
        const std::array<xr64::rage_wars::type1d::Descriptor,
                xr64::rage_wars::type1d::kDescriptorCount> &descriptors) {
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const gpr destination = n64_address(
                kType1dRuntimeAddress + static_cast<std::uint32_t>(index * 0x0CU));
        MEM_W(0, destination) = static_cast<std::int32_t>(descriptors[index].event);
        MEM_W(4, destination) = static_cast<std::int32_t>(descriptors[index].key);
        MEM_W(8, destination) = static_cast<std::int32_t>(descriptors[index].callback);
    }
    std::fprintf(stderr,
            "RW029_TYPE1D_PUBLISHED address=0x%08X count=%zu first_callback=0x%08X last_callback=0x%08X transform=event,key,callback\n",
            kType1dRuntimeAddress, descriptors.size(), descriptors.front().callback,
            descriptors.back().callback);
    std::fflush(stderr);
}

bool resolve_runtime_tlb(std::uint32_t address, std::uint32_t &physical) {
    for (const xr64::rage_wars::gate12::TlbEntry &entry : g_runtime.tlb) {
        if (!entry.valid) continue;
        const std::uint32_t half_size = (entry.page_mask + 0x2000U) >> 1U;
        const std::uint32_t pair_size = half_size * 2U;
        const std::uint32_t pair_mask = ~(pair_size - 1U);
        if ((address & pair_mask) != (entry.entry_hi & pair_mask)) continue;
        const bool odd = (address & half_size) != 0;
        const std::uint32_t page = odd ? entry.physical_odd : entry.physical_even;
        if (page != 0xFFFFFFFFU) {
            physical = page + (address & (half_size - 1U));
            return true;
        }
        return false;
    }
    return false;
}

// Donor scheduler contract: guest code observes the OSThread pointer that was
// actually activated, not merely the thread selected by a start request.  The
// fresh host launches each registered guest thread at this boundary, so mirror
// the activated thread immediately before re-entering generated code.
void publish_fresh_thread_activation(std::uint8_t *rdram,
        const HostThreadState &thread) {
    MEM_W(0, n64_address(0x800D3F20U)) =
            static_cast<std::int32_t>(thread.thread);
    std::fprintf(stderr,
            "RW019_THREAD_ACTIVATED thread=0x%08X entry=0x%08X guest_current=0x%08X\n",
            thread.thread, thread.entry,
            static_cast<std::uint32_t>(MEM_W(0, n64_address(0x800D3F20U))));
    std::fflush(stderr);
}

thread_local std::jmp_buf g_host_exit_jump;
thread_local bool g_host_exit_armed = false;

[[noreturn]] void exit_host_thread() {
    if (!g_host_exit_armed) std::abort();
    std::longjmp(g_host_exit_jump, 1);
}

[[noreturn]] void stop_resident(
        xr64::rage_wars::gate12::BoundaryKind boundary, std::uint32_t address) {
    std::fprintf(stderr, "G6 stop_resident boundary=%u address=0x%08X host_thread=0x%08X armed=%d\n",
            static_cast<unsigned>(boundary), address, g_host_thread, g_host_exit_armed ? 1 : 0);
    std::fflush(stderr);
    if (g_host_thread != 0) {
        std::unique_lock<std::mutex> lock(g_host_mutex);
        {
            if (g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::none ||
                    g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::resident_main_return) {
                g_runtime.boundary = boundary;
                g_runtime.boundary_address = address;
            }
        }
        g_host_cv.notify_all();
        g_host_cv.wait(lock, [] { return g_host_stop; });
        lock.unlock();
        exit_host_thread();
    }
    g_runtime.boundary = boundary;
    g_runtime.boundary_address = address;
    g_resident_running = false;
    std::longjmp(g_resident_boundary_jump, 1);
}

extern "C" void gate7_func_0023C1D0(std::uint8_t *rdram, recomp_context *ctx);

extern "C" std::uint8_t *xr64_recomp_resolve_address_impl(
        std::uint8_t *rdram, gpr raw_address) {
    const std::uint32_t address = static_cast<std::uint32_t>(raw_address);
    if (std::uint8_t *asset_endpoint = rage_wars::resolve_asset_loader_guest_address(address)) {
        return asset_endpoint;
    }
    std::uint32_t physical = 0xFFFFFFFFU;
    if ((address >= 0x80000000U && address < xr64::rage_wars::memory_profile::kKseg0End) ||
            (address >= 0xA0000000U && address < xr64::rage_wars::memory_profile::kKseg1End)) {
        physical = address & 0x007FFFFFU;
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    } else if (address >= 0x00200000U && address < 0x00400000U) {
        // Gate 4's model-C audit proves that boot TLB entry 31 aliases this
        // complete two-megabyte guest image onto physical RDRAM at zero.
        // N64ModernRuntime does not expose that boot entry to this host-side
        // resolver, so preserve the proven translation explicitly.
        physical = address - 0x00200000U;
#endif
    } else if (address < 0x80000000U) {
        for (const xr64::rage_wars::gate12::TlbEntry &entry : g_runtime.tlb) {
            if (!entry.valid) continue;
            const std::uint32_t half_size = (entry.page_mask + 0x2000U) >> 1U;
            const std::uint32_t pair_size = half_size * 2U;
            const std::uint32_t pair_mask = ~(pair_size - 1U);
            if ((address & pair_mask) != (entry.entry_hi & pair_mask)) continue;
            const bool odd = (address & half_size) != 0;
            const std::uint32_t page = odd ? entry.physical_odd : entry.physical_even;
            if (page != 0xFFFFFFFFU) {
                physical = page + (address & (half_size - 1U));
            }
            break;
        }
    }

    // An explicitly published ROM-backed VM alias is the fallback for a
    // KUSEG miss. It must be considered after the guest TLB lookup, but
    // before delivering a guest TLB fault: the startup heap is intentionally
    // published this way until the game installs its page mapping.
    if (physical == 0xFFFFFFFFU && address < 0x80000000U) {
        std::lock_guard<std::mutex> lock(g_vm_mutex);
        for (HostVmRegion &region : g_vm_regions) {
            const std::uint32_t size = static_cast<std::uint32_t>(region.bytes.size());
            if (address >= region.base && address - region.base < size) {
                return region.bytes.data() + (address - region.base);
            }
        }
    }

    if (physical == 0xFFFFFFFFU && address < 0x80000000U && !g_in_tlb_fault) {
        if (std::getenv("XR64_RW_TLB_TRACE") != nullptr) {
            static unsigned trace_count = 0;
            if (trace_count++ < 8) {
                std::fprintf(stderr,
                        "RW032_TLB_FAULT address=0x%08X host_thread=0x%08X host_entry=0x%08X\n",
                        address, g_host_thread, g_host_entry);
                std::fflush(stderr);
            }
        }
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
        if (ultramodern::this_thread() != NULLPTR) {
            g_in_tlb_fault = true;
            const bool delivered = ultramodern::raise_fault(rdram, 8U, address);
            g_in_tlb_fault = false;
            if (!delivered) {
                std::fprintf(stderr,
                        "RW027_FAULT_UNDELIVERED thread=0x%08X address=0x%08X\n",
                        static_cast<unsigned>(ultramodern::this_thread()), address);
                std::fflush(stderr);
            }
        }
#else
        if (g_host_thread != 0 && g_host_context != nullptr) {
            const std::uint32_t thread_physical = g_host_thread & 0x007FFFFFU;
            if (thread_physical + 0x128U <= kRdramSize) {
                *reinterpret_cast<std::int32_t *>(rdram + thread_physical + 0x120U) = 8;
                *reinterpret_cast<std::int32_t *>(rdram + thread_physical + 0x124U) =
                        static_cast<std::int32_t>(address);
                recomp_context fault_context = *g_host_context;
                fault_context.f_odd = &fault_context.f1.u32l;
                g_in_tlb_fault = true;
                gate7_func_0023C1D0(rdram, &fault_context);
                g_in_tlb_fault = false;
            }
        }
#endif
        for (const xr64::rage_wars::gate12::TlbEntry &entry : g_runtime.tlb) {
            if (!entry.valid) continue;
            const std::uint32_t half_size = (entry.page_mask + 0x2000U) >> 1U;
            const std::uint32_t pair_size = half_size * 2U;
            const std::uint32_t pair_mask = ~(pair_size - 1U);
            if ((address & pair_mask) != (entry.entry_hi & pair_mask)) continue;
            const bool odd = (address & half_size) != 0;
            const std::uint32_t page = odd ? entry.physical_odd : entry.physical_even;
            if (page != 0xFFFFFFFFU) {
                physical = page + (address & (half_size - 1U));
            }
            break;
        }
    }

    if (physical < kRdramSize) {
        return rdram + physical;
    }
    if (address < 0x80000000U) {
        std::lock_guard<std::mutex> lock(g_vm_mutex);
        for (HostVmRegion &region : g_vm_regions) {
            const std::uint32_t size = static_cast<std::uint32_t>(region.bytes.size());
            if (address >= region.base && address - region.base < size) {
                return region.bytes.data() + (address - region.base);
            }
        }
    }
#if defined(_WIN32)
    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto return_rva = return_address - module_base;
#else
    const std::uintptr_t return_rva = 0;
#endif
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (std::getenv("XR64_RW021_NATIVE_TRACE") != nullptr && address == 0x0044A9C0U) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
        const auto runtime_thread = static_cast<std::uint32_t>(ultramodern::this_thread());
#else
        const std::uint32_t runtime_thread = 0;
#endif
        std::fprintf(stderr,
                "RW021_UNRESOLVED_REQUEST address=0x%08X runtime_thread=0x%08X host_thread=0x%08X "
                "host_entry=0x%08X host_context=%d caller_rva=0x%llX\n",
                address, runtime_thread, g_host_thread, g_host_entry, g_host_context != nullptr,
                static_cast<unsigned long long>(return_rva));
    }
#endif
    std::fprintf(stderr,
            "G15 unresolved_memory thread=0x%08X entry=0x%08X address=0x%08X caller_rva=0x%llX\n",
            g_host_thread, g_host_entry, address, static_cast<unsigned long long>(return_rva));
#if defined(_WIN32)
    void *frames[16]{};
    const auto frame_count = CaptureStackBackTrace(0, 16, frames, nullptr);
    for (unsigned short index = 0; index < frame_count; ++index) {
        const auto frame = reinterpret_cast<std::uintptr_t>(frames[index]);
        if (frame >= module_base && frame < module_base + 0x20000000ULL) {
            std::fprintf(stderr, "G15 resolver_stack[%hu]=0x%llX\n", index,
                    static_cast<unsigned long long>(frame - module_base));
        }
    }
#endif
    std::fflush(stderr);
    if (g_resident_running) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, address);
    }
    fail("unmapped or MMIO guest memory access at " +
            std::to_string(static_cast<unsigned long long>(address)));
}

void run_host_thread(std::uint8_t *rdram, const std::shared_ptr<HostThreadState> &thread) {
    g_host_exit_armed = true;
    if (setjmp(g_host_exit_jump) == 0) {
    try {
        std::fprintf(stderr, "G14 thread_run thread=0x%08X entry=0x%08X sp=0x%08X arg=0x%08X\n", thread->thread, thread->entry, thread->stack, thread->argument);
        std::fflush(stderr);
        g_host_thread = thread->thread;
        g_host_entry = thread->entry;
        g_host_argument = thread->argument;
        publish_fresh_thread_activation(rdram, *thread);
        recomp_context context{};
        context.r4 = static_cast<gpr>(static_cast<std::int32_t>(thread->argument));
        context.r29 = static_cast<gpr>(static_cast<std::int32_t>(thread->stack));
        context.status_reg = static_cast<std::uint32_t>(MEM_W(0x118, n64_address(thread->thread)));
        context.f_odd = &context.f1.u32l;
        context.mips3_float_mode = true;
        g_host_context = &context;
        recomp_func_t *entry = ::get_function_with_context(
                rdram, &context, static_cast<std::int32_t>(thread->entry));
        if (entry == nullptr) {
            stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, thread->entry);
        }
        entry(rdram, &context);
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::resident_main_return, thread->entry);
    } catch (const std::exception &exception) {
        std::lock_guard<std::mutex> lock(g_host_mutex);
        if (g_async_error.empty()) g_async_error = exception.what();
        g_host_stop = true;
        g_host_cv.notify_all();
    }
    }
    g_host_exit_armed = false;
    g_host_context = nullptr;
    g_host_thread = 0;
}

bool enqueue_host_message(std::uint8_t *rdram, std::uint32_t queue, std::uint32_t message) {
    const gpr queue_address = n64_address(queue);
    const std::int32_t count = static_cast<std::int32_t>(MEM_W(0x10, queue_address));
    const std::int32_t valid = static_cast<std::int32_t>(MEM_W(0x08, queue_address));
    if (count <= 0 || valid < 0 || valid >= count) return false;
    const std::int32_t first = static_cast<std::int32_t>(MEM_W(0x0C, queue_address));
    const std::uint32_t buffer = static_cast<std::uint32_t>(MEM_W(0x14, queue_address));
    const std::int32_t index = (first + valid) % count;
    MEM_W(index * 4, n64_address(buffer)) = static_cast<std::int32_t>(message);
    MEM_W(0x08, queue_address) = valid + 1;
    return true;
}

void deliver_graphics_event(std::uint8_t *rdram, std::uint32_t event,
        const char *name, std::uint64_t task_number, std::uint32_t task_address,
        std::uint32_t display_list, std::uint32_t producer_return) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    if (event == 4U) {
        ultramodern::send_sp_message();
        return;
    }
    if (event == 9U) {
        ultramodern::send_dp_message();
        return;
    }
#endif
    std::lock_guard<std::mutex> lock(g_host_mutex);
    const gpr event_entry = n64_address(0x80147B10U + event * 8U);
    const std::uint32_t queue = static_cast<std::uint32_t>(MEM_W(0x00, event_entry));
    const std::uint32_t message = static_cast<std::uint32_t>(MEM_W(0x04, event_entry));
    if (queue == 0) {
        std::fprintf(stderr,
                "RW023_GRAPHICS_COMPLETION event=%s queue=0x%08X msg=0x%08X "
                "result=not-registered\n", name, queue, message);
        std::fflush(stderr);
        return;
    }
    const gpr queue_entry = n64_address(queue);
    const int valid_before = static_cast<int>(MEM_W(0x08, queue_entry));
    const bool delivered = enqueue_host_message(rdram, queue, message);
    std::fprintf(stderr,
            "RW023_GRAPHICS_COMPLETION event=%s task=%llu vi=%llu "
            "queue=0x%08X msg=0x%08X valid_before=%d result=%s\n", name,
            static_cast<unsigned long long>(task_number),
            static_cast<unsigned long long>(g_vi_29a_enqueued.load()), queue,
            message, valid_before, delivered ? "delivered" : "queue-full");
    std::fflush(stderr);
    if (delivered) {
        g_graphics_completions.push_back({task_number, task_address, display_list,
                producer_return, g_vi_29a_enqueued.load(), message});
        g_host_cv.notify_all();
    }
}

void deliver_fresh_si_message() {
    std::lock_guard<std::mutex> lock(g_host_mutex);
    if (g_si_event_rdram == nullptr || g_si_event_queue == 0) {
        std::fprintf(stderr, "RW017_SI_ADAPTER queue=0x%08X msg=0x%08X result=not-registered\n",
                g_si_event_queue, g_si_event_message);
        std::fflush(stderr);
        return;
    }
    const bool delivered = enqueue_host_message(
            g_si_event_rdram, g_si_event_queue, g_si_event_message);
    const gpr queue_memory = n64_address(g_si_event_queue);
    std::uint8_t *rdram = g_si_event_rdram;
    std::fprintf(stderr, "RW017_SI_ADAPTER queue=0x%08X msg=0x%08X result=%d\n",
            g_si_event_queue, g_si_event_message, delivered ? 1 : 0);
    std::fprintf(stderr, "RW017_SI_ADAPTER_STATE queue=0x%08X count=%d valid=%d\n",
            g_si_event_queue, static_cast<int>(MEM_W(0x10, queue_memory)),
            static_cast<int>(MEM_W(0x08, queue_memory)));
    std::fflush(stderr);
    if (delivered) g_host_cv.notify_all();
}

void checkpoint_vi_tick() {
    ++g_vi_ticks;
}

void checkpoint_vi_message(std::uint8_t *rdram, std::uint32_t queue, std::uint32_t message) {
    std::lock_guard<std::mutex> lock(g_host_mutex);
    if (!enqueue_host_message(rdram, queue, message)) return;
    if (queue == 0x80140968U && message == 0x29AU) {
        const std::uint64_t retrace = ++g_vi_29a_enqueued;
        std::fprintf(stderr,
                "RW023_VI_RETRACE retrace=%llu ticks=%llu graphics_tasks=%llu "
                "queue=0x%08X msg=0x%08X\n",
                static_cast<unsigned long long>(retrace),
                static_cast<unsigned long long>(g_vi_ticks.load()),
                static_cast<unsigned long long>(g_graphics_task_number), queue,
                message);
        std::fprintf(stderr, "G14 vi_enqueue queue=0x%08X msg=0x%08X\n", queue, message);
        std::fflush(stderr);
    }
    g_host_cv.notify_all();
}
bool jam_host_message(std::uint8_t *rdram, std::uint32_t queue, std::uint32_t message) {
    const gpr queue_address = n64_address(queue);
    const std::int32_t count = static_cast<std::int32_t>(MEM_W(0x10, queue_address));
    const std::int32_t valid = static_cast<std::int32_t>(MEM_W(0x08, queue_address));
    if (count <= 0 || valid < 0 || valid >= count) return false;
    const std::int32_t first = static_cast<std::int32_t>(MEM_W(0x0C, queue_address));
    const std::int32_t new_first = (first + count - 1) % count;
    const std::uint32_t buffer = static_cast<std::uint32_t>(MEM_W(0x14, queue_address));
    MEM_W(new_first * 4, n64_address(buffer)) = static_cast<std::int32_t>(message);
    MEM_W(0x0C, queue_address) = new_first;
    MEM_W(0x08, queue_address) = valid + 1;
    return true;
}


void dispatch_next_thread(std::uint8_t *rdram) {
    constexpr std::int32_t kRunQueue = static_cast<std::int32_t>(0x800D3F18U);
    constexpr std::int32_t kRunningThread = static_cast<std::int32_t>(0x800D3F20U);
    constexpr std::uint32_t kThreadTail = 0x800D3F10U;

    ++g_runtime.service_calls;
    ++g_runtime.scheduler_dispatch_calls;
    if (g_runtime.scheduler_dispatch_calls > 8) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::scheduler_dispatch, 0x002BC4A0U);
    }

    const gpr thread = static_cast<gpr>(MEM_W(kRunQueue, 0));
    if (thread == 0 || static_cast<std::uint32_t>(thread) == kThreadTail) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::scheduler_dispatch, 0x002BC4A0U);
    }

    g_runtime.dispatched_thread = static_cast<std::uint32_t>(thread);
    g_runtime.dispatched_entry = static_cast<std::uint32_t>(MEM_W(0x11C, thread));
    MEM_W(kRunQueue, 0) = MEM_W(0, thread);
    MEM_W(kRunningThread, 0) = static_cast<std::int32_t>(thread);
    MEM_H(0x10, thread) = 4;

    recomp_context thread_context{};
    thread_context.r4 = LD(thread, 0x38);
    thread_context.r29 = LD(thread, 0xF0);
    thread_context.status_reg = static_cast<std::uint32_t>(MEM_W(0x118, thread));
    thread_context.f_odd = &thread_context.f1.u32l;
    thread_context.mips3_float_mode = true;

    recomp_func_t *entry = ::get_function_with_context(
            rdram, &thread_context,
            static_cast<std::int32_t>(g_runtime.dispatched_entry));
    if (entry == nullptr) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, g_runtime.dispatched_entry);
    }
    entry(rdram, &thread_context);
    stop_resident(xr64::rage_wars::gate12::BoundaryKind::resident_main_return, g_runtime.dispatched_entry);
}

std::vector<std::uint8_t> read_rom(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("could not open ROM: " + path.string());
    std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() != kRomSize) fail("ROM must be exactly 8 MiB");
    if (!std::equal(kInternalName.begin(), kInternalName.end(), bytes.begin() + 0x20)) {
        fail("N64 internal name does not match Turok: Rage Wars");
    }
    const std::array<std::uint8_t, 4> z64_magic{0x80, 0x37, 0x12, 0x40};
    if (!std::equal(z64_magic.begin(), z64_magic.end(), bytes.begin())) {
        fail("ROM is not big-endian .z64 byte order");
    }
    return bytes;
}

void initial_dma(const std::vector<std::uint8_t> &rom, std::vector<std::uint8_t> &rdram) {
    for (std::size_t index = 0; index < kInitialDmaSize; ++index) {
        rdram[(kEntrypointPhysical + index) ^ 3U] = rom[kInitialRomOffset + index];
    }
}

void map_rom_vm_page(const std::vector<std::uint8_t> &rom,
        std::uint32_t guest_base, std::uint32_t rom_source, std::uint32_t size) {
    HostVmRegion region;
    region.base = guest_base;
    region.source = rom_source;
    region.bytes.resize(size);
    for (std::uint32_t index = 0; index < size; ++index) {
        region.bytes[index ^ 3U] = rom[rom_source + index];
    }
    std::lock_guard<std::mutex> lock(g_vm_mutex);
    g_vm_regions.push_back(std::move(region));
}

std::size_t validate_function_table() {
    std::size_t function_count = 0;
    for (std::size_t section_index = 0; section_index < kSectionTableEntryCount; ++section_index) {
        const SectionTableEntry &section = section_table[section_index];
        if (section.index >= num_sections) {
            fail("section ID exceeds logical section count");
        }
        for (std::size_t function_index = 0; function_index < section.num_funcs; ++function_index) {
            const FuncEntry &entry = section.funcs[function_index];
            if (entry.func == nullptr) fail("null generated function pointer");
            const std::uint32_t address = section.ram_addr + entry.offset;
            if (::recomp::overlays::get_generated_callable(
                        static_cast<std::int32_t>(address)) == nullptr) {
                fail("generated lookup missing at VRAM 0x" + std::to_string(address));
            }
            ++function_count;
        }
    }
    return function_count;
}

bool materialize_code_page(std::uint8_t *rdram, std::uint32_t vram) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    const std::span<const std::uint8_t> rom = recomp::get_rom();
#else
    if (g_rom == nullptr) return false;
    const std::span<const std::uint8_t> rom = *g_rom;
#endif
    if (rom.empty()) return false;
    std::lock_guard<std::mutex> lock(g_code_residency_mutex);

    const SectionTableEntry *target = nullptr;
    for (std::size_t index = 0; index < kSectionTableEntryCount; ++index) {
        const SectionTableEntry &section = section_table[index];
        const std::uint64_t section_end = static_cast<std::uint64_t>(section.ram_addr) + section.size;
        if (section.rom_addr != 0 && vram >= section.ram_addr && vram < section_end) {
            target = &section;
            break;
        }
    }
    if (target == nullptr) return false;

    const std::uint32_t guest_page = vram & ~0xFFFU;
    const std::uint32_t guest_section_offset = target->ram_addr - guest_page;
    if (guest_section_offset >= 0x1000U || target->rom_addr < guest_section_offset) {
        return false;
    }

    std::uint32_t physical_page = 0;
    std::uint32_t loaded_physical = 0;
    std::uint64_t resident_storage = 0;
    std::uint32_t resident_pages = 0;
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    const std::uint32_t rom_page = target->rom_addr - guest_section_offset;
    const std::uint64_t initial_rom_end =
            static_cast<std::uint64_t>(kInitialRomOffset) + kInitialDmaSize;
    if (rom_page < kInitialRomOffset ||
            static_cast<std::uint64_t>(rom_page) + 0x1000U > initial_rom_end) {
        return false;
    }
    physical_page = static_cast<std::uint32_t>(
            kEntrypointPhysical + (rom_page - kInitialRomOffset));
    loaded_physical = physical_page + guest_section_offset;
    resident_storage = 0x1000U;
    resident_pages = 1;
    if ((physical_page & 0xFFFU) != 0 ||
            static_cast<std::uint64_t>(physical_page) + resident_storage > kRdramSize ||
            static_cast<std::uint64_t>(target->rom_addr) + target->size > rom.size()) {
        return false;
    }
    for (std::uint32_t index = 0; index < target->size; ++index) {
        if (rdram[(loaded_physical + index) ^ 3U] != rom[target->rom_addr + index]) {
            return false;
        }
    }
    if (::recomp::overlays::find_resident_callable(
                static_cast<std::int32_t>(loaded_physical)) == nullptr) {
        load_overlays(rom_page, static_cast<std::int32_t>(physical_page), 0x1000U);
    }
#else
    physical_page = 0x0014D000U +
            static_cast<std::uint32_t>(g_code_page_materializations) * 0x1000U;
    loaded_physical = physical_page + guest_section_offset;
    const std::uint64_t resident_bytes =
            static_cast<std::uint64_t>(guest_section_offset) + target->size;
    resident_storage =
            (resident_bytes + 0xFFFU) & ~static_cast<std::uint64_t>(0xFFFU);
    resident_pages = static_cast<std::uint32_t>(resident_storage / 0x1000U);
    if (resident_pages == 0 ||
            static_cast<std::uint64_t>(physical_page) + resident_storage > kRdramSize ||
            static_cast<std::uint64_t>(target->rom_addr) + target->size > rom.size()) {
        return false;
    }
    for (std::uint32_t index = 0; index < target->size; ++index) {
        rdram[(loaded_physical + index) ^ 3U] = rom[target->rom_addr + index];
    }
    recomp::overlays::register_rom_dma(target->rom_addr,
            static_cast<std::int32_t>(loaded_physical), target->size);
#endif

    const std::uint32_t pair_base = guest_page & ~0x1FFFU;
    std::uint32_t tlb_index = 0xFFFFFFFFU;
    std::uint32_t preserved_even = 0xFFFFFFFFU;
    std::uint32_t preserved_odd = 0xFFFFFFFFU;
    for (std::uint32_t index = 0; index < g_runtime.tlb.size(); ++index) {
        const auto &entry = g_runtime.tlb[index];
        const std::uint32_t half_size = (entry.page_mask + 0x2000U) >> 1U;
        const std::uint32_t entry_pair_base = entry.entry_hi & ~(half_size * 2U - 1U);
        if (entry.valid && half_size == 0x1000U && entry_pair_base == pair_base) {
            tlb_index = index;
            preserved_even = entry.physical_even;
            preserved_odd = entry.physical_odd;
            break;
        }
    }
    if (tlb_index == 0xFFFFFFFFU) {
        for (std::uint32_t index = 0; index < g_runtime.tlb.size(); ++index) {
            if (!g_runtime.tlb[index].valid) {
                tlb_index = index;
                break;
            }
        }
    }
    if (tlb_index == 0xFFFFFFFFU) return false;

    if (guest_page == pair_base) {
        preserved_even = physical_page;
    } else {
        preserved_odd = physical_page;
    }

    recomp_context map_context{};
    map_context.r4 = static_cast<gpr>(tlb_index);
    map_context.r5 = 0;
    map_context.r6 = static_cast<gpr>(static_cast<std::int32_t>(pair_base));
    map_context.r7 = static_cast<gpr>(static_cast<std::int32_t>(preserved_even));
    map_context.r29 = n64_address(0x803FF000U);
    map_context.f_odd = &map_context.f1.u32l;
    map_context.mips3_float_mode = true;
    MEM_W(0x10, map_context.r29) = static_cast<std::int32_t>(preserved_odd);
    MEM_W(0x14, map_context.r29) = 7;
    xr64::rage_wars::gate12::map_tlb(g_runtime, rdram, map_context);

    std::uint32_t physical = 0xFFFFFFFFU;
    if (!resolve_runtime_tlb(vram, physical)) return false;
    g_code_page_materializations += resident_pages;
    std::fprintf(stderr,
            "RW010_CODE_PAGE guest=0x%08X rom=0x%08X physical=0x%08X section=%zu size=0x%X pages=%u\n",
            guest_page, target->rom_addr, loaded_physical, target->index, target->size,
            resident_pages);
    std::fflush(stderr);
    return true;
}

void require_lookup(std::uint32_t address) {
    if (get_function(static_cast<std::int32_t>(address)) == nullptr) {
        fail("required lookup missing at " + std::to_string(address));
    }
}

void reset_checkpoint_state() {
    g_guest_timer_producer.stop();
    g_missing_lookup = 0;
    g_intercept_main = false;
    g_main_boundary_reached = false;
    g_entrypoint_break_reached = false;
    g_resident_running = false;
    g_runtime = {};
    g_rom = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_vm_mutex);
        g_vm_regions.clear();
        g_vm_next_page = 0x800U;
    }
    {
        std::lock_guard<std::mutex> lock(g_host_mutex);
        if (!g_host_workers.empty()) fail("previous host workers were not joined");
        g_host_threads.clear();
        g_async_error.clear();
        g_host_stop = false;
        g_si_event_rdram = nullptr;
        g_si_event_queue = 0;
        g_si_event_message = 0;
        g_host_threads_created = 0;
        g_host_threads_started = 0;
        g_host_queue_receives = 0;
        g_host_queue_sends = 0;
    g_main_frame_wait_calls = 0;
        g_synthetic_retraces = 0;
        g_synthetic_retraces_consumed = 0;
        g_graphics_task_number = 0;
        g_graphics_task_generations = 0;
        g_graphics_completions.clear();
    }
    g_code_residency_requests.store(0);
    g_code_page_materializations = 0;
    g_vi_ticks = 0;
    g_vi_29a_enqueued = 0;
    g_vi_29a_received = 0;
    g_host_thread = 0;
    g_host_entry = 0;
    g_host_argument = 0;
    g_host_context = nullptr;
    g_in_tlb_fault = false;
    g_sp_raw_task = 0;
    {
        std::lock_guard<std::mutex> lock(g_graphics_capture_mutex);
        g_graphics_capture = {};
    }
}

void stop_and_join_host_workers() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(g_host_mutex);
        g_host_stop = true;
        workers.swap(g_host_workers);
    }
    g_host_cv.notify_all();
    g_guest_timer_producer.stop();
    for (std::thread &worker : workers) {
        if (worker.joinable()) worker.join();
    }
    g_rom = nullptr;
    g_resident_running = false;
}

void boot_boundary_main(std::uint8_t *, recomp_context *ctx) {
    g_main_boundary_reached = true;
    if (static_cast<std::uint32_t>(ctx->r4) != kMainAddress) fail("main boundary register mismatch");
}

void rage_wars_break_handler(std::uint32_t vram) {
    if (g_intercept_main && vram == kEntrypointBreak) {
        g_entrypoint_break_reached = true;
        return;
    }
    if (g_resident_running) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::mips_break, vram);
    }
    fail("MIPS break at " + std::to_string(vram));
}

void rage_wars_pause_handler(std::uint8_t *) {
    stop_resident(xr64::rage_wars::gate12::BoundaryKind::pause_self, 0);
}

}  // namespace

std::span<const std::uint8_t> xr64_recomp_rom() {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    return recomp::get_rom();
#else
    if (g_rom == nullptr) {
        return {};
    }
    return *g_rom;
#endif
}

extern "C" std::uint8_t *xr64_recomp_resolve_address_base(
        std::uint8_t *rdram, gpr address) {
    return xr64_recomp_resolve_address_impl(rdram, address);
}

extern "C" std::uint8_t *xr64_recomp_resolve_address(
        std::uint8_t *rdram, gpr address) {
#if defined(PCRAGE_RECOVERY_ASSET_RESOLVER)
    // The descriptor resolver supplies explicit cart/asset aliases, but a
    // valid guest TLB mapping is authoritative for KUSEG accesses and must
    // win before descriptor or eager backing is considered.
    const std::uint32_t guest_address = static_cast<std::uint32_t>(address);
    if (guest_address < 0x80000000U) {
        std::uint32_t physical = 0xFFFFFFFFU;
        if (resolve_runtime_tlb(guest_address, physical) && physical < kRdramSize) {
            return rdram + physical;
        }
    }
    return pcrage_recovery_memory(rdram, address);
#else
    return xr64_recomp_resolve_address_base(rdram, address);
#endif
}

extern "C" void xr64_native_menu_trace(const char *event, std::uint8_t *,
        gpr value0, gpr value1, gpr value2) {
    const char *trace_path = std::getenv("XR64_NATIVE_MENU_ROW_TRACE");
    if (trace_path == nullptr || trace_path[0] == '\0') return;

    static std::mutex trace_mutex;
    static std::atomic<std::uint64_t> sequence{0};
    const std::lock_guard<std::mutex> lock(trace_mutex);
    std::FILE *trace = std::fopen(trace_path, "ab");
    if (trace == nullptr) return;
    const std::uint64_t ordinal = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
#if defined(_WIN32)
    const unsigned long thread_id = GetCurrentThreadId();
#else
    const unsigned long thread_id = 0;
#endif
    std::fprintf(trace,
            "%llu thread=%lu event=%s value0=0x%08X value1=0x%08X value2=0x%08X\n",
            static_cast<unsigned long long>(ordinal), thread_id, event,
            static_cast<std::uint32_t>(value0), static_cast<std::uint32_t>(value1),
            static_cast<std::uint32_t>(value2));
    std::fclose(trace);
}

extern "C" recomp_func_t *xr64_rage_wars_get_function_with_context_trace(
        std::uint8_t *rdram, recomp_context *ctx, std::int32_t vram,
        const char *file, int line) {
    xr64::rage_wars::crash::note_lookup(vram, file, line);
    if (static_cast<std::uint32_t>(vram) == 0x80000400U &&
            std::getenv("XR64_RW_RESOLVER_TRACE") != nullptr) {
        std::fprintf(stderr,
                "RW031_LOOKUP_CALLSITE vram=0x%08X host_thread=0x%08X host_entry=0x%08X file=%s line=%d\n",
                static_cast<std::uint32_t>(vram), g_host_thread, g_host_entry, file, line);
        std::fflush(stderr);
    }
    return ::get_function_with_context(rdram, ctx, vram);
}


extern "C" void xr64_rage_wars_note_recovered_rom_entry(
        std::uint32_t original_vram, std::uint32_t source_rom,
        std::uint32_t logical_section, recomp_context* ctx) {
    // Bound first-entry records independently from replay/capture diagnostics.
    // These identify compiled original-ROM bodies, not inferred loaded mappings.
    static std::atomic<bool> seen[512]{};
    constexpr std::uint32_t first_recovery_section = 3871;
    if (logical_section < first_recovery_section ||
            logical_section - first_recovery_section >= 512 ||
            seen[logical_section - first_recovery_section].exchange(true)) return;
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "RW_ROM_BODY_ENTRY original_vram=0x%08X source_rom=0x%08X logical_section=%u raw_ra=0x%08X raw_sp=0x%08X\n",
        original_vram, source_rom, logical_section,
        ctx ? static_cast<std::uint32_t>(ctx->r31) : UINT32_MAX,
        ctx ? static_cast<std::uint32_t>(ctx->r29) : UINT32_MAX);
    std::fputs(line, stderr);
}

extern "C" void xr64_rage_wars_invoke_function_with_context_trace(
        std::uint8_t* rdram, recomp_context* ctx, std::int32_t vram,
        std::uint32_t caller_pc, std::uint32_t caller_rom, std::uint32_t caller_function_rom,
        std::uint32_t caller_section_rom, std::uint32_t caller_section,
        const char* file, int line) {
    const auto token = xr64::rage_wars::crash::begin_indirect_call(
        vram, caller_pc, caller_rom, caller_function_rom, caller_section_rom, caller_section, file, line, ctx);
    try {
        recomp::overlays::CallableMappingIdentity mapping{};
        recomp_func_t* entry = recomp::overlays::resolve_callable_with_identity(rdram, ctx, vram, mapping);
        xr64::rage_wars::crash::CallIdentity evidence{};
        evidence.active_callable = mapping.active_callable;
        evidence.tlb_bound = mapping.tlb_bound; evidence.match_count = mapping.match_count;
        evidence.section_rom = mapping.section_rom; evidence.function_rom = mapping.function_rom;
        evidence.original_function_vram = mapping.original_function_vram;
        evidence.loaded_section_vram = mapping.loaded_section_vram;
        evidence.physical_section_addr = mapping.physical_section_addr;
        evidence.logical_section_index = static_cast<std::uint32_t>(mapping.logical_section_index);
        evidence.code_section_index = static_cast<std::uint32_t>(mapping.code_section_index);
        evidence.native_function = reinterpret_cast<std::uintptr_t>(entry);
        xr64::rage_wars::crash::resolved_indirect_call(token, evidence);
        entry(rdram, ctx);
    } catch (...) {
        xr64::rage_wars::crash::complete_indirect_call(token, true);
        throw;
    }
    xr64::rage_wars::crash::complete_indirect_call(token, false);
}

extern "C" {

bool rage_wars_surface_unresolved_callable_fault(
        std::uint8_t *rdram, recomp_context *ctx, std::uint32_t vram) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    if (materialize_code_page(rdram, vram)) return true;
    std::uint32_t physical = 0xFFFFFFFFU;
    const bool eligible = vram < 0x80000000U &&
            ultramodern::this_thread() != NULLPTR && !g_in_tlb_fault &&
            !resolve_runtime_tlb(vram, physical);
    if (!eligible) {
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
        if (std::getenv("XR64_RW031_CALLABLE_FAULT_TRACE") != nullptr) {
            std::fprintf(stderr,
                    "RW031_EXEC_FAULT_DECLINED thread=0x%08X vram=0x%08X mapped=%d in_fault=%d\n",
                    static_cast<unsigned>(ultramodern::this_thread()), vram,
                    physical != 0xFFFFFFFFU ? 1 : 0, g_in_tlb_fault ? 1 : 0);
            std::fflush(stderr);
        }
#endif
        return false;
    }

    g_in_tlb_fault = true;
    const bool delivered = ultramodern::raise_fault(rdram, 8U, vram);
    g_in_tlb_fault = false;

    physical = 0xFFFFFFFFU;
    const bool mapped_after_resume = resolve_runtime_tlb(vram, physical);
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (std::getenv("XR64_RW031_CALLABLE_FAULT_TRACE") != nullptr) {
        std::fprintf(stderr,
                "RW031_EXEC_FAULT_RESULT thread=0x%08X vram=0x%08X delivered=%d mapped=%d physical=0x%08X\n",
                static_cast<unsigned>(ultramodern::this_thread()), vram,
                delivered ? 1 : 0, mapped_after_resume ? 1 : 0, physical);
        std::fflush(stderr);
    }
#endif
    return delivered && mapped_after_resume;
#else
    ++g_code_residency_requests;
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (std::getenv("XR64_RW_RESOLVER_TRACE") != nullptr) {
        std::fprintf(stderr,
                "RW031_CALLABLE_REQUEST vram=0x%08X host_thread=0x%08X host_entry=0x%08X\n",
                vram, g_host_thread, g_host_entry);
        std::fflush(stderr);
    }
#endif
    if (materialize_code_page(rdram, vram)) return true;

    std::uint32_t physical = 0xFFFFFFFFU;
    const bool eligible = vram < 0x80000000U && g_host_thread != 0 &&
            g_host_context != nullptr && !g_in_tlb_fault &&
            !resolve_runtime_tlb(vram, physical);
    if (!eligible) return false;

    const std::uint32_t thread_physical = g_host_thread & 0x007FFFFFU;
    if (thread_physical + 0x128U > kRdramSize) return false;
    *reinterpret_cast<std::int32_t *>(rdram + thread_physical + 0x120U) = 8;
    *reinterpret_cast<std::int32_t *>(rdram + thread_physical + 0x124U) =
            static_cast<std::int32_t>(vram);
    recomp_context fault_context = *g_host_context;
    fault_context.f_odd = &fault_context.f1.u32l;
    g_in_tlb_fault = true;
    gate7_func_0023C1D0(rdram, &fault_context);
    g_in_tlb_fault = false;

    physical = 0xFFFFFFFFU;
    return resolve_runtime_tlb(vram, physical);
#endif
}

bool rage_wars_validate_declared_entry_provenance(
        std::uint8_t *rdram, std::int32_t signed_vram,
        recomp_func_t *declared_function) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    if (rdram == nullptr || declared_function == nullptr) return false;
    const std::uint32_t vram = static_cast<std::uint32_t>(signed_vram);
    if (vram >= 0x80000000U) return false;

    std::lock_guard<std::mutex> lock(g_code_residency_mutex);
    const SectionTableEntry *target = nullptr;
    for (std::size_t section_index = 0;
            section_index < kSectionTableEntryCount && target == nullptr;
            ++section_index) {
        const SectionTableEntry &section = section_table[section_index];
        if (section.rom_addr == 0 || vram < section.ram_addr ||
                vram >= static_cast<std::uint64_t>(section.ram_addr) + section.size) {
            continue;
        }
        for (std::size_t function_index = 0;
                function_index < section.num_funcs; ++function_index) {
            const FuncEntry &function = section.funcs[function_index];
            if (section.ram_addr + function.offset == vram &&
                    function.func == declared_function) {
                target = &section;
                break;
            }
        }
    }
    if (target == nullptr) return false;

    std::uint32_t physical = 0xFFFFFFFFU;
    if (!resolve_runtime_tlb(vram, physical) || physical >= kRdramSize) {
        return false;
    }
    const std::uint32_t entry_offset = vram - target->ram_addr;
    const std::uint32_t rom_entry = target->rom_addr + entry_offset;
    const std::uint32_t available = (std::min)({
            target->size - entry_offset,
            0x1000U - (vram & 0xFFFU),
            0x1000U - (physical & 0xFFFU)});
    const std::span<const std::uint8_t> rom = recomp::get_rom();
    if (available == 0 ||
            static_cast<std::uint64_t>(physical) + available > kRdramSize ||
            static_cast<std::uint64_t>(rom_entry) + available > rom.size()) {
        return false;
    }
    for (std::uint32_t index = 0; index < available; ++index) {
        if (rdram[(physical + index) ^ 3U] != rom[rom_entry + index]) {
            return false;
        }
    }
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (std::getenv("XR64_RW031_CALLABLE_FAULT_TRACE") != nullptr) {
        std::fprintf(stderr,
                "RW031_DECLARED_PROVENANCE vram=0x%08X physical=0x%08X rom=0x%08X bytes=0x%X result=accepted\n",
                vram, physical, rom_entry, available);
        std::fflush(stderr);
    }
#endif
    return true;
#else
    (void)rdram;
    (void)signed_vram;
    (void)declared_function;
    return false;
#endif
}

#if !defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
#endif
void recomp_syscall_handler(std::uint8_t *, recomp_context *, std::int32_t vram) {
    if (g_resident_running) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::syscall, static_cast<std::uint32_t>(vram));
    }
    fail("unhandled syscall at " + std::to_string(static_cast<std::uint32_t>(vram)));
}
void gate7_func_002BACA0(std::uint8_t *, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    ctx->r2 = ultramodern::get_faulted_thread();
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (std::getenv("XR64_RW027_DESCRIPTOR_TRACE") != nullptr) {
        std::fprintf(stderr,
                "RW027_FORWARDER consumer=0x%08X faulted_thread=0x%08X\n",
                static_cast<unsigned>(ultramodern::this_thread()),
                static_cast<unsigned>(ctx->r2));
        std::fflush(stderr);
    }
#endif
#else
    ctx->r2 = n64_address(g_host_thread);
#endif
}
void resident_wave11_func_0023C76C(std::uint8_t *, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    const auto &rom = recomp::get_rom();
#else
    if (g_rom == nullptr) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x0023C76CU);
    }
    const auto &rom = *g_rom;
#endif
    const std::uint32_t requested = static_cast<std::uint32_t>(ctx->r4);
    const std::uint32_t source = static_cast<std::uint32_t>(ctx->r6) & 0x0FFFFFFFU;
    const std::uint32_t pages = (requested + 0xFFFU) >> 12U;
    if (pages == 0 || pages > (0x7FFFFFFFU >> 12U)) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x0023C76CU);
    }
    const std::uint32_t byte_count = pages << 12U;
    if (source > rom.size() || byte_count > rom.size() - source) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, source);
    }

    HostVmRegion region;
    {
        std::lock_guard<std::mutex> lock(g_vm_mutex);
        const std::uint32_t aligned_page = (g_vm_next_page + 1U) & ~1U;
        const std::uint32_t base_page = aligned_page < 0x800U ? 0x800U : aligned_page;
        if (base_page > (0x7FFFFFFFU >> 12U) - pages) {
            stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x0023C76CU);
        }
        region.base = base_page << 12U;
        region.source = source;
        region.bytes.assign(byte_count, 0);
        for (std::uint32_t index = 0; index < byte_count; ++index) {
            region.bytes[index ^ 3U] = rom[source + index];
        }
        g_vm_next_page = base_page + pages;
        ctx->r2 = static_cast<gpr>(static_cast<std::int32_t>(region.base));
        std::fprintf(stderr,
                "G15 vm_eager base=0x%08X source=0x%08X requested=%u pages=%u\n",
                region.base, source, requested, pages);
        std::fflush(stderr);
        g_vm_regions.push_back(std::move(region));
    }
}
void gate4_func_002AD2A0(std::uint8_t *rdram, recomp_context *ctx) { xr64::rage_wars::gate12::map_tlb(g_runtime, rdram, *ctx); }
void gate4_func_002B9CA0(std::uint8_t *, recomp_context *ctx) { xr64::rage_wars::gate12::si_device_busy(g_runtime, *ctx); }
void resident_wave9_func_002B9BE0(std::uint8_t *rdram, recomp_context *ctx) {
    const std::uint32_t direction = static_cast<std::uint32_t>(ctx->r4);
    const std::uint32_t guest_buffer = static_cast<std::uint32_t>(ctx->r5);
    const bool si_trace=std::getenv("XR64_XR_SI_TRACE")!=nullptr;
    if(si_trace) std::fprintf(stderr,"RW114_SI_DMA phase=begin direction=%u buffer=%08X state=%u ra=%08X thread=%08X\n",
        direction,guest_buffer,unsigned(rdram[0x109BB8^3U]),unsigned(ctx->r31),unsigned(ultramodern::this_thread()));
    const int result = rage_wars::controller_service().submit_dma(
            rdram, kRdramSize,
            direction, guest_buffer);
    if (const char *trace = std::getenv("XR64_RW_LEGACY_TRACE"); trace && trace[0] == '1')
    std::fprintf(stderr, "RW017_SI_DMA direction=%u buffer=0x%08X result=%d\n",
            direction, guest_buffer, result);
    std::fflush(stderr);
    if(si_trace) std::fprintf(stderr,"RW114_SI_DMA phase=end direction=%u result=%d state=%u thread=%08X\n",
        direction,result,unsigned(rdram[0x109BB8^3U]),unsigned(ultramodern::this_thread()));
    ctx->r2 = result;
}
void func_002BAD70(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    osInitialize_recomp(rdram, ctx);
    // N64ModernRuntime installs the ROM image after GameEntry::on_init. Restore
    // the IPL3-owned boot word at the guest osInitialize boundary where Rage
    // Wars will subsequently consume it.
    MEM_W(0, n64_address(0x80000300U)) = 1;
    return;
#endif
    // N64ModernRuntime's __osInitialize_common translation delegates to an empty host osInitialize.
    ++g_runtime.service_calls;
}
void func_002BCE60(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    __osDisableInt_recomp(rdram, ctx);
    return;
#else
    xr64::rage_wars::gate12::disable_interrupts(g_runtime, *ctx);
#endif
}
void func_002BCE80(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    __osRestoreInt_recomp(rdram, ctx);
    return;
#else
    xr64::rage_wars::gate12::restore_interrupts(g_runtime, *ctx);
#endif
}
void gate5_shared_002BC29C(std::uint8_t *rdram, recomp_context *) {
    ++g_runtime.scheduler_yield_calls;
    dispatch_next_thread(rdram);
}
void gate5_shared_002BC4A0(std::uint8_t *rdram, recomp_context *) { dispatch_next_thread(rdram); }
void gate7_host_002AD320(std::uint8_t *, recomp_context *ctx) { xr64::rage_wars::gate12::unmap_tlb(g_runtime, *ctx); }
int xr64_rage_wars_native_os_get_time(std::uint8_t *, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERN_VI_ADAPTERS)
    // The native VI service replaces the guest manager that accumulates
    // __osCurrentTime at 0x80147B98. The original osGetTime body would read
    // its unmaintained zero base and expose only the 32-bit Count register.
    // Use the same monotonic timer owner, retaining all counter bits.
    xr64::rage_wars::guest_clock::write_os_time_result(
            *ctx, g_guest_timer_producer.current_ticks());
    return 1;
#else
    // The standalone guest-VI path retains the original libultra routine.
    return 0;
#endif
}

void gate7_host_002BCE30(std::uint8_t *, recomp_context *ctx) {
    ++g_runtime.service_calls;
    ++g_runtime.count_calls;
    ctx->r2 = static_cast<gpr>(g_guest_timer_producer.current_count());
}
void gate7_host_002BCFB0(std::uint8_t *, recomp_context *ctx) { xr64::rage_wars::gate12::probe_tlb(g_runtime, *ctx); }
void gate7_host_002BD0A0(std::uint8_t *, recomp_context *ctx) { xr64::rage_wars::gate12::set_interrupt_mask(g_runtime, *ctx); }
void gate7_host_002BD220(std::uint8_t *, recomp_context *) { xr64::rage_wars::gate12::cache_operation(g_runtime); }
void resident_wave9_host_002BCEA0(std::uint8_t *, recomp_context *) { xr64::rage_wars::gate12::cache_operation(g_runtime); }
void resident_wave11_func_002BACD0(std::uint8_t *rdram, recomp_context *ctx) {
    const std::uint32_t requested = static_cast<std::uint32_t>(ctx->r4);
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    osGetThreadPri_recomp(rdram, ctx);
    return;
#endif
    const std::uint32_t thread = requested != 0 ? requested : g_host_thread;
    ctx->r2 = thread != 0
            ? static_cast<gpr>(static_cast<std::int32_t>(MEM_W(0x04, n64_address(thread))))
            : 0;
}

void resident_wave11_func_002BA280(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERN_VI_ADAPTERS)
    const gpr manager = n64_address(0x800D30D0U);
    if (MEM_W(0, manager) != 0) return;

    std::fprintf(stderr, "G14 vi_manager boundary=0x002BA280\n");
    std::fflush(stderr);
    if (ultramodern::is_vi_initialized()) {
        std::fprintf(stderr, "G14 vi_manager host_service=already_initialized skip_init\n");
        std::fflush(stderr);
    } else {
        std::fprintf(stderr, "G14 vi_manager host_service=uninitialized init_once\n");
        std::fflush(stderr);
        osCreateViManager_recomp(rdram, ctx);
    }

    // The historical native runtime kept this guest-visible manager contract
    // while leaving VI cadence and delivery to the host.  Preserve the list,
    // internal queue/messages, event registrations, and dormant thread state;
    // do not enter the generated MMIO-backed VI manager loop.
    resident_wave11_func_002BB928(rdram, ctx);

    const gpr vi_queue = n64_address(0x80147A80U);
    const gpr retrace_message = n64_address(0x80147AC0U);
    const gpr counter_message = n64_address(0x80147AE0U);
    recomp_context setup = *ctx;
    setup.r4 = vi_queue;
    setup.r5 = n64_address(0x80147AA0U);
    setup.r6 = 5;
    resident_wave9_func_002BAB90(rdram, &setup);

    MEM_H(0, retrace_message) = 0xDU;
    MEM_B(2, retrace_message) = 0;
    MEM_W(4, retrace_message) = 0;
    MEM_H(0, counter_message) = 0xEU;
    MEM_B(2, counter_message) = 0;
    MEM_W(4, counter_message) = 0;

    setup.r4 = 7;
    setup.r5 = vi_queue;
    setup.r6 = retrace_message;
    resident_wave11_func_002BB480(rdram, &setup);
    setup.r4 = 3;
    setup.r6 = counter_message;
    resident_wave11_func_002BB480(rdram, &setup);

    const gpr manager_thread = n64_address(0x80146850U);
    MEM_W(0, manager_thread) = 0;
    MEM_W(8, manager_thread) = 0;
    MEM_H(0x10, manager_thread) = 1;
    MEM_H(0x12, manager_thread) = 0;
    MEM_W(0x14, manager_thread) = 0;
    MEM_W(0x18, manager_thread) = 0;
    MEM_W(4, manager_thread) = ctx->r4;
    MEM_W(0x38, manager_thread) = -1;
    MEM_W(0x3C, manager_thread) = static_cast<std::int32_t>(manager);
    MEM_W(0xF0, manager_thread) = -1;
    MEM_W(0xF4, manager_thread) = static_cast<std::int32_t>(0x80148000U);
    MEM_W(0x118, manager_thread) = static_cast<std::int32_t>(0x0400FF03U);
    MEM_W(0x11C, manager_thread) = static_cast<std::int32_t>(0x002BA3F0U);

    MEM_W(4, manager) = static_cast<std::int32_t>(0x80146850U);
    MEM_W(8, manager) = static_cast<std::int32_t>(0x80147A80U);
    MEM_W(0xC, manager) = static_cast<std::int32_t>(0x80147A80U);
    MEM_W(0x10, manager) = 0;
    MEM_W(0x14, manager) = 0;
    MEM_W(0x18, manager) = 0;
    MEM_W(0, manager) = 1;
    return;
#endif
    stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x002BA280U);
}

void resident_wave14_func_002BA580(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERN_VI_ADAPTERS)
    std::fprintf(stderr, "G14 vi_set_event queue=0x%08X msg=0x%08X retrace=%u\n",
            static_cast<unsigned>(ctx->r4), static_cast<unsigned>(ctx->r5),
            static_cast<unsigned>(ctx->r6));
    std::fflush(stderr);
    osViSetEvent_recomp(rdram, ctx);
    return;
#endif
    stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x002BA580U);
}
void resident_wave11_host_002BD160(std::uint8_t *, recomp_context *) { xr64::rage_wars::gate12::unmap_tlb_all(g_runtime); }
void resident_wave9_func_002B71F0(std::uint8_t *, recomp_context *ctx) {
    if (xr64::rage_wars::audio::enabled()) {
        ctx->r2 = xr64::rage_wars::audio::fifo_full() ?
            static_cast<gpr>(static_cast<std::int32_t>(0x80000000U)) : 0;
        return;
    }
    // AI_STATUS_REG bit 31 reports a full FIFO. Keep the audio-disabled menu
    // checkpoint on the ROM's real defer branch until the host owns AI DMA;
    // this avoids pretending that an audio buffer was submitted.
    ctx->r2 = static_cast<gpr>(static_cast<std::int32_t>(0x80000000U));
}

void resident_wave11_func_002B7210(std::uint8_t *rdram, recomp_context *ctx) {
    // OS audio-rate setup computes a legal divisor, then writes AI device
    // registers at 0xA4500000. Preserve its observable return value while the
    // host owns the audio device rather than mirroring MMIO into RDRAM.
    const std::int32_t requested_rate = static_cast<std::int32_t>(ctx->r4);
    if (requested_rate <= 0) {
        ctx->r2 = static_cast<gpr>(-1);
        return;
    }
    const std::int32_t clock = MEM_W(0, n64_address(0x800D3F00U));
    std::uint32_t offset_bits = static_cast<std::uint32_t>(MEM_W(0, n64_address(0x800C7848U)));
    std::uint32_t wrap_bits = static_cast<std::uint32_t>(MEM_W(0, n64_address(0x800C784CU)));
    float offset;
    float wrap;
    std::memcpy(&offset, &offset_bits, sizeof(offset));
    std::memcpy(&wrap, &wrap_bits, sizeof(wrap));
    float divisor_value = static_cast<float>(clock) / static_cast<float>(requested_rate) + offset;
    if (divisor_value >= wrap) divisor_value -= wrap;
    const std::int32_t divisor = static_cast<std::int32_t>(divisor_value);
    ctx->r2 = static_cast<gpr>(divisor < 0x84 ? -1 : clock / divisor);
    if(divisor>=0x84) ultramodern::set_audio_frequency(static_cast<std::uint32_t>(clock/divisor));
}

void set_raw_virtual_bit(std::uint8_t *rdram, gpr base, std::uint32_t bit) {
    const std::uint32_t address = static_cast<std::uint32_t>(base) + (bit >> 3U);
    // Rage Wars uses low virtual addresses for this one boot-time bitfield.
    // The generated MEM_* helpers model KSEG0 only, so preserve the MIPS byte
    // layout directly against the physical RDRAM mirror.
    rdram[(address & 0x007FFFFFU) ^ 3U] |= static_cast<std::uint8_t>(1U << (bit & 7U));
}

void set_dispatcher_object_flag(std::uint8_t *rdram, gpr object, std::uint32_t flag) {
    std::uint32_t offset = 0x7BU;
    if (flag == 0x10U || flag == 0x11U) {
        offset = 0x78U;
        flag -= 0x10U;
    }
    set_raw_virtual_bit(rdram, ADD32(object, offset), flag);
}
void resident_wave15_external_00256E48(std::uint8_t *, recomp_context *) {
    stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x00256E48U);
}
void resident_wave15_external_002B2400(std::uint8_t *, recomp_context *) {
    stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x002B2400U);
}
#if 0 // Legacy diagnostic stub superseded by the exact 0x00411E4C helper below.
void resident_wave14_func_00411E4C(std::uint8_t *, recomp_context *) {
    // The sole table-base writer at 0x0040FC14 has not run before this
    // unconditional overlay-constructor lookup. No ROM DMA initializes the
    // global, and no active TLB entry makes address zero valid at this gate.
    stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x00411E4CU);
}
#endif
void resident_wave14_func_00411E4C(std::uint8_t *rdram, recomp_context *ctx) {
    ctx->r2 = S32(ctx->r4 << 1);
    ctx->r2 = ADD32(ctx->r2, ctx->r4);
    ctx->r3 = S32(0X8015 << 16);
    ctx->r3 = MEM_W(-0X464C, ctx->r3);
    ctx->r2 = S32(ctx->r2 << 2);
    ctx->r2 = ADD32(ctx->r2, ctx->r3);
    ctx->r2 = MEM_W(0X8, ctx->r2);
}



void virtual_wave14_func_00444260(std::uint8_t *rdram, recomp_context *ctx) {
    static constexpr std::array<std::uint8_t, 8> kFlags{2, 14, 6, 7, 16, 5, 17, 6};
    static constexpr std::array<bool, 8> kUseObjectFlags{true, false, true, false, true, true, true, false};

    // Keep the KSEG0 pointer sign-extended: MEM_* subtracts KSEG0 from its
    // gpr operand, and a uint32_t conversion would address 4 GiB past RDRAM.
    const gpr object = ctx->r4;
    const std::uint32_t state = static_cast<std::uint8_t>(MEM_B(0x7F, object));
    if (state >= kFlags.size()) {
        const gpr defaults = static_cast<gpr>(MEM_W(0, n64_address(0x800D28F0U)));
        MEM_W(0x84, object) = MEM_W(0, defaults);
        MEM_W(0x88, object) = MEM_W(4, defaults);
        return;
    }

    // The ROM jump table enters eight case labels inside 0x004251F4. At this
    // call site s0 is the object index and s1 is the owning object block.
    if (kUseObjectFlags[state]) {
        set_dispatcher_object_flag(rdram, ctx->r16, kFlags[state]);
    } else {
        set_raw_virtual_bit(rdram, ADD32(ctx->r16, 0x79), kFlags[state]);
    }
    ctx->r2 = ctx->r17;
}

void gate7_func_002BB1D0(std::uint8_t *rdram, recomp_context *ctx);

void resident_wave14_external_0025E1B4(std::uint8_t *, recomp_context *) {
    // Gate 3: ROM-zero four-byte manifest placeholder; no generated direct
    // caller and no authoritative indirect reference. This is the sole no-op.
}

void resident_wave14_external_002A55B0(std::uint8_t *rdram, recomp_context *ctx) {
    const gpr manager = ctx->r4;
    const std::uint32_t first_head = static_cast<std::uint32_t>(MEM_W(0x7528, manager));
    const std::uint32_t second_head = static_cast<std::uint32_t>(MEM_W(0x7594, manager));
    if (first_head != 0 || second_head != 0) {
        std::fprintf(stderr, "GFX_CAPTURE guard_2A55B0_rejected manager=0x%08X heads=0x%08X,0x%08X\n",
                static_cast<unsigned>(manager), first_head, second_head);
        std::fflush(stderr);
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x002A55B0U);
    }
    MEM_W(0, n64_address(0x800DDAF8U)) = 0;
}

void resident_wave9_func_002B9DA8(std::uint8_t *, recomp_context *ctx) {
    g_sp_raw_task = static_cast<std::uint32_t>(ctx->r4);
    ctx->r2 = ctx->r4;
}

void resident_wave9_func_002B9EAC(std::uint8_t *rdram, recomp_context *ctx) {
    const std::uint32_t raw_task = static_cast<std::uint32_t>(ctx->r4);
    if (raw_task != g_sp_raw_task) stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x002B9EACU);
    const std::uint32_t physical = raw_task & 0x007FFFFFU;
    if(physical <= kRdramSize-0x40U && MEM_W(0,n64_address(raw_task))==2 && xr64::rage_wars::audio::enabled()) {
        const OSTask* audio_task=reinterpret_cast<const OSTask*>(rdram+physical);
        static std::uint64_t audio_tasks=0;
        if(audio_tasks<4)std::fprintf(stderr,"RW_AUDIO_TASK task=0x%08X ucode=0x%08X data=0x%08X commands=0x%08X/%u\n",raw_task,audio_task->t.ucode,audio_task->t.ucode_data,audio_task->t.data_ptr,audio_task->t.data_size);
        {
            namespace t = xr64::rage_wars::audio::telemetry;
            t::increment(t::Counter::tasks);
            t::Scope rsp_timing(t::Timer::rsp);
            if(!recomp::rsp::run_task(rdram,audio_task))stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup,0x002B9EACU);
        }
        deliver_graphics_event(rdram,4U,"audio-sp",++audio_tasks,raw_task,audio_task->t.data_ptr,static_cast<std::uint32_t>(ctx->r31));
        return;
    }
    if (physical > kRdramSize - 0x40U || static_cast<std::uint32_t>(MEM_W(0, n64_address(raw_task))) != 1U) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x002B9EACU);
    }
    const std::uint64_t task_number = ++g_graphics_task_number;
    const std::uint32_t display_list = static_cast<std::uint32_t>(
            MEM_W(0x30, n64_address(raw_task)));
    if (const char *v = std::getenv("XR64_RW_LEGACY_TRACE"); v && v[0] == '1') std::fprintf(stderr,
            "RW023_TASK_SUBMIT task=%llu vi=%llu task_guest=0x%08X "
            "dl_guest=0x%08X producer_return=0x%08X consumer_thread=0x%08X "
            "consumer_entry=0x%08X\n",
            static_cast<unsigned long long>(task_number),
            static_cast<unsigned long long>(g_vi_29a_enqueued.load()), raw_task,
            display_list, static_cast<unsigned>(ctx->r31), g_host_thread,
            g_host_entry);
    if (const char *v = std::getenv("XR64_RW_LEGACY_TRACE"); v && v[0] == '1') std::fprintf(stderr, "RW018_OSTASK_SUBMIT task=0x%08X thread=0x%08X entry=0x%08X\n",
            raw_task, g_host_thread, g_host_entry);
    std::fflush(stderr);
    std::string renderer_error;
    deliver_graphics_event(rdram, 4U, "sp", task_number, raw_task, display_list,
            static_cast<std::uint32_t>(ctx->r31));
    if (!g_desktop_renderer.submit_task(rdram, kRdramSize, raw_task, renderer_error)) {
        std::fprintf(stderr, "RW012_RENDERER_TASK_REJECTED task=0x%08X detail=%s\n",
                raw_task, renderer_error.c_str());
        std::fflush(stderr);
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::graphics_task_capture,
                raw_task);
    } else {
        const auto &stats = g_desktop_renderer.stats();
        if (const char *v = std::getenv("XR64_RW_LEGACY_TRACE"); v && v[0] == '1') std::fprintf(stderr,
                "RW018_RENDERED_FRAME task=0x%08X presented=%llu triangles=%u\n",
                raw_task, static_cast<unsigned long long>(stats.presented_frames),
                stats.last_triangles);
        std::fflush(stderr);
    }
    deliver_graphics_event(rdram, 9U, "dp", task_number, raw_task, display_list,
            static_cast<std::uint32_t>(ctx->r31));
}
void gate4_func_002BABC0(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    osCreateThread_recomp(rdram, ctx);
    return;
#endif
    const std::uint32_t thread = static_cast<std::uint32_t>(ctx->r4);
    const std::uint32_t entry = static_cast<std::uint32_t>(ctx->r6);
    std::fprintf(stderr, "G14 create_enter a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X sp=0x%08X\n", (unsigned)ctx->r4, (unsigned)ctx->r5, (unsigned)ctx->r6, (unsigned)ctx->r7, (unsigned)ctx->r29);
    std::fflush(stderr);
    const std::uint32_t argument = static_cast<std::uint32_t>(ctx->r7);
    const std::uint32_t stack = static_cast<std::uint32_t>(MEM_W(0x10, ctx->r29)) - 0x10U;
    const std::int32_t priority = static_cast<std::int32_t>(MEM_W(0x14, ctx->r29));
    const gpr thread_memory = n64_address(thread);
    MEM_W(0x00, thread_memory) = 0;
    MEM_W(0x08, thread_memory) = 0;
    std::fprintf(stderr, "G14 thread_create thread=0x%08X entry=0x%08X sp=0x%08X arg=0x%08X pri=%d\n", thread, entry, stack, argument, priority);
    std::fflush(stderr);
    MEM_H(0x10, thread_memory) = 1;
    MEM_H(0x12, thread_memory) = 0;
    MEM_W(0x14, thread_memory) = static_cast<std::int32_t>(ctx->r5);
    MEM_W(0x18, thread_memory) = 0;
    MEM_W(0x04, thread_memory) = priority;
    MEM_W(0x38, thread_memory) = static_cast<std::int32_t>(argument) < 0 ? -1 : 0;
    MEM_W(0x3C, thread_memory) = static_cast<std::int32_t>(argument);
    MEM_W(0xF0, thread_memory) = static_cast<std::int32_t>(stack) < 0 ? -1 : 0;
    MEM_W(0xF4, thread_memory) = static_cast<std::int32_t>(stack);
    MEM_W(0x118, thread_memory) = static_cast<std::int32_t>(0x0400FF03U);
    MEM_W(0x11C, thread_memory) = static_cast<std::int32_t>(entry);
    auto state = std::make_shared<HostThreadState>();
    state->thread = thread;
    state->entry = entry;
    state->argument = argument;
    state->stack = stack;
    {
        std::lock_guard<std::mutex> lock(g_host_mutex);
        g_host_threads[thread] = std::move(state);
        ++g_host_threads_created;
    }
}

void gate4_func_002BB680(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    osStartThread_recomp(rdram, ctx);
    return;
#endif
    const std::uint32_t thread_address = static_cast<std::uint32_t>(ctx->r4);
    std::shared_ptr<HostThreadState> thread;
    {
        std::lock_guard<std::mutex> lock(g_host_mutex);
        const auto found = g_host_threads.find(thread_address);
        if (found == g_host_threads.end()) {
            stop_resident(xr64::rage_wars::gate12::BoundaryKind::scheduler_dispatch, 0x002BB680U);
        }
        thread = found->second;
        if (thread->started) return;
        if (g_host_stop) return;
        std::fprintf(stderr, "G14 thread_start thread=0x%08X entry=0x%08X\n", thread->thread, thread->entry);
        std::fflush(stderr);
        thread->started = true;
        ++g_host_threads_started;
        g_host_workers.emplace_back(run_host_thread, rdram, thread);
    }
}

void resident_wave9_func_002BB520(std::uint8_t *rdram, recomp_context *ctx) {
    if (g_host_entry == kIdleThreadEntry && static_cast<std::uint32_t>(ctx->r4) == 0) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    pause_self(rdram);
    return;
#endif
        std::unique_lock<std::mutex> lock(g_host_mutex);
        g_host_cv.wait(lock, [] { return g_host_stop; });
        lock.unlock();
        exit_host_thread();
    }
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    // Guest 0x002BB520 is osSetThreadPri. Pause-menu worker draining
    // requires priority changes and the runtime's resulting reschedule.
    osSetThreadPri_recomp(rdram, ctx);
#endif
}
void resident_wave14_func_002B9970(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    const std::uint32_t runtime_direction = static_cast<std::uint32_t>(ctx->r4);
    const std::uint32_t runtime_source = static_cast<std::uint32_t>(ctx->r5) & 0x0FFFFFFFU;
    const std::uint32_t runtime_destination = static_cast<std::uint32_t>(ctx->r6) & 0x007FFFFFU;
    const std::uint32_t runtime_length = static_cast<std::uint32_t>(ctx->r7);
    const auto runtime_rom = recomp::get_rom();
    if (runtime_direction != 0 || runtime_source > runtime_rom.size() ||
            runtime_length > runtime_rom.size() - runtime_source ||
            runtime_destination > kRdramSize || runtime_length > kRdramSize - runtime_destination) {
        ultramodern::error_handling::message_box("Rage Wars PI DMA metadata is outside the Gate 6 ROM/RDRAM bounds");
        ctx->r2 = static_cast<gpr>(-1);
        return;
    }
    rage_wars::asset_loader().begin_transfer();
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    const bool rw021_target_raw_pi = std::getenv("XR64_RW021_NATIVE_TRACE") != nullptr
            && runtime_direction == 0 && runtime_source == 0x0028B244U
            && runtime_destination == 0x001820C0U && runtime_length == 0x20U;
    if (rw021_target_raw_pi) {
        std::fprintf(stderr,
                "RW021_RAW_PI_ENTER current_thread=0x%08X ra=0x%08X sp=0x%08X direction=%u "
                "source=0x%08X destination=0x%08X length=0x%X\n",
                static_cast<unsigned>(ultramodern::this_thread()), static_cast<unsigned>(ctx->r31),
                static_cast<unsigned>(ctx->r29), runtime_direction, runtime_source,
                runtime_destination, runtime_length);
        std::fflush(stderr);
    }
#endif
    std::uint32_t guest_destination = runtime_destination;
    if (runtime_destination >= 0x00700000U) {
        guest_destination = runtime_destination - 0x00300000U;
    } else if (runtime_destination < 0x00100000U) {
        guest_destination = runtime_destination + 0x00200000U;
    }
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    for (const std::uint32_t confirm_target : {0x00426270U, 0x00423930U, 0x00435E98U, 0x00438BA4U}) {
    if (guest_destination <= confirm_target &&
            confirm_target - guest_destination < runtime_length) {
        std::fprintf(stderr,
                "RW082_CHARACTER_CONFIRM_OVERLAY target=0x%08X target_rom=0x%08X dma_source=0x%08X physical_destination=0x%08X guest_destination=0x%08X length=0x%X\n",
                confirm_target, runtime_source + (confirm_target - guest_destination),
                runtime_source, runtime_destination, guest_destination, runtime_length);
        std::fflush(stderr);
    }
    }
#endif
    {
        std::lock_guard<std::mutex> residency_lock(g_code_residency_mutex);
        for (std::uint32_t index = 0; index < runtime_length; ++index) {
            rdram[(runtime_destination + index) ^ 3U] = runtime_rom[runtime_source + index];
        }
        recomp::overlays::register_rom_dma(
            runtime_source, static_cast<std::int32_t>(guest_destination), runtime_length);
    }
    // Data/code publication occurs while the transfer is busy.  The status
    // becomes idle before completion delivery, so the game observes the native
    // asset contract rather than a PI/TLB implementation detail.
    rage_wars::asset_loader().complete_transfer();
    ultramodern::pi_complete();
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw021_target_raw_pi) {
        std::fprintf(stderr,
                "RW021_RAW_PI_CALLBACK current_thread=0x%08X ra=0x%08X sp=0x%08X\n",
                static_cast<unsigned>(ultramodern::this_thread()), static_cast<unsigned>(ctx->r31),
                static_cast<unsigned>(ctx->r29));
        std::fflush(stderr);
    }
#endif
    ctx->r2 = 0;
    return;
#endif
    const std::uint32_t direction = static_cast<std::uint32_t>(ctx->r4);
    const std::uint32_t source = static_cast<std::uint32_t>(ctx->r5) & 0x0FFFFFFFU;
    const std::uint32_t destination = static_cast<std::uint32_t>(ctx->r6) & 0x007FFFFFU;
    const std::uint32_t length = static_cast<std::uint32_t>(ctx->r7);
    if (direction != 0 || g_rom == nullptr || source > g_rom->size() ||
            length > g_rom->size() - source || destination > kRdramSize ||
            length > kRdramSize - destination) {
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, 0x002B9970U);
    }
    for (std::uint32_t index = 0; index < length; ++index) {
        rdram[(destination + index) ^ 3U] = (*g_rom)[source + index];
    }
    const bool covers_constructor =
            destination <= 0x0074A79CU && length > 0x0074A79CU - destination;
    const bool covers_dynamic_target =
            destination <= 0x0074DAA4U && length > 0x0074DAA4U - destination;
    if (covers_constructor || covers_dynamic_target) {
        std::fprintf(stderr,
                "G15 overlay_dma source=0x%08X destination=0x%08X length=%u\n",
                source, destination, length);
        std::fflush(stderr);
    }
    if (g_host_entry == 0x0025651CU) {
        const std::uint32_t pi_reply_queue = g_host_argument + 0xA48U;
        {
            std::lock_guard<std::mutex> lock(g_host_mutex);
            if (!enqueue_host_message(rdram, pi_reply_queue, 0)) {
                stop_resident(xr64::rage_wars::gate12::BoundaryKind::scheduler_dispatch, 0x002B9970U);
            }
        }
        g_host_cv.notify_all();
    }
    ctx->r2 = 0;
}


void gate7_func_002BB1D0(std::uint8_t *rdram, recomp_context *ctx) {
    const std::uint32_t queue = static_cast<std::uint32_t>(ctx->r4);
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    const bool rw021_work_queue = std::getenv("XR64_RW021_SENDER_TRACE") != nullptr && queue == 0x800FF3E8U;
    const std::uint32_t rw021_output = static_cast<std::uint32_t>(ctx->r5);
#endif
    const bool si_trace=queue==0x80109C00U && std::getenv("XR64_XR_SI_TRACE")!=nullptr;
    if(si_trace) std::fprintf(stderr,"RW114_SI_RECV phase=begin ra=%08X state=%u count=%u thread=%08X\n",
        unsigned(ctx->r31),unsigned(rdram[0x109BB8^3U]),unsigned(MEM_W(8,n64_address(queue))),unsigned(ultramodern::this_thread()));
    osRecvMesg_recomp(rdram, ctx);
    if(si_trace) std::fprintf(stderr,"RW114_SI_RECV phase=end result=%d state=%u count=%u thread=%08X\n",
        int(ctx->r2),unsigned(rdram[0x109BB8^3U]),unsigned(MEM_W(8,n64_address(queue))),unsigned(ultramodern::this_thread()));

#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (rw021_work_queue && static_cast<std::int32_t>(ctx->r2) == 0 && rw021_output != 0) {
        const std::uint32_t item = static_cast<std::uint32_t>(MEM_W(0, n64_address(rw021_output)));
        std::fprintf(stderr,
                "RW021_WORK_RECV current_thread=0x%08X item=0x%08X word0=0x%08X word1=0x%08X "
                "word2=0x%08X word3=0x%08X ra=0x%08X sp=0x%08X\n",
                static_cast<unsigned>(ultramodern::this_thread()), item,
                static_cast<unsigned>(MEM_W(0, n64_address(item))), static_cast<unsigned>(MEM_W(4, n64_address(item))),
                static_cast<unsigned>(MEM_W(8, n64_address(item))), static_cast<unsigned>(MEM_W(12, n64_address(item))),
                static_cast<unsigned>(ctx->r31), static_cast<unsigned>(ctx->r29));
        std::fflush(stderr);
    }
#endif
    return;
#endif
    const std::uint32_t output = static_cast<std::uint32_t>(ctx->r5);
    const bool block = static_cast<std::int32_t>(ctx->r6) != 0;
    std::fprintf(stderr, "G14 recv_enter entry=0x%08X queue=0x%08X out=0x%08X flags=%d\n", g_host_entry, queue, output, block ? 1 : 0);
    std::fflush(stderr);
    std::unique_lock<std::mutex> lock(g_host_mutex);
    ++g_host_queue_receives;
    const gpr queue_memory = n64_address(queue);
    const bool main_frame_queue = g_host_entry == kMainThreadEntry &&
            static_cast<std::int32_t>(MEM_W(0x10, queue_memory)) == 0x1000;
    const bool si_completion_queue = queue == 0x80109C00U;
    if (si_completion_queue) {
        std::fprintf(stderr, "RW017_SI_WAIT rdram=%p queue=0x%08X count=%d valid=%d\n",
                static_cast<void *>(rdram), queue,
                static_cast<int>(MEM_W(0x10, queue_memory)),
                static_cast<int>(MEM_W(0x08, queue_memory)));
        std::fflush(stderr);
    }
    if (main_frame_queue) {
        ++g_main_frame_wait_calls;
        g_host_cv.notify_all();
    }
    while (static_cast<std::int32_t>(MEM_W(0x08, queue_memory)) == 0) {
        if (main_frame_queue && g_synthetic_retraces < capture_retrace_budget()) {
            MEM_W(0, n64_address(kSyntheticRetraceMessage)) = kOsScRetraceMsg;
            if (!enqueue_host_message(rdram, queue, kSyntheticRetraceMessage)) {
                stop_resident(xr64::rage_wars::gate12::BoundaryKind::scheduler_dispatch, 0x002BB1D0U);
            }
            ++g_synthetic_retraces;
            break;
        }
        if (!block) {
            ctx->r2 = static_cast<gpr>(-1);
            return;
        }
        g_host_cv.wait(lock, [&] {
            return g_host_stop || g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::graphics_task_capture ||
                    static_cast<std::int32_t>(MEM_W(0x08, queue_memory)) != 0;
        });
        if (si_completion_queue) {
            std::fprintf(stderr, "RW017_SI_WAKE rdram=%p queue=0x%08X valid=%d stop=%d boundary=%u\n",
                    static_cast<void *>(rdram), queue,
                    static_cast<int>(MEM_W(0x08, queue_memory)), g_host_stop ? 1 : 0,
                    static_cast<unsigned>(g_runtime.boundary));
            std::fflush(stderr);
        }
        if (g_host_stop) {
            lock.unlock();
            exit_host_thread();
        }
        if (g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::graphics_task_capture) {
            lock.unlock();
            stop_resident(g_runtime.boundary, g_runtime.boundary_address);
        }
    }
    const std::int32_t count = static_cast<std::int32_t>(MEM_W(0x10, queue_memory));
    const std::int32_t first = static_cast<std::int32_t>(MEM_W(0x0C, queue_memory));
    const std::uint32_t buffer = static_cast<std::uint32_t>(MEM_W(0x14, queue_memory));
    if (si_completion_queue) {
        std::fprintf(stderr, "RW017_SI_QUEUE_FIELDS queue=0x%08X count=%d first=%d buffer=0x%08X\n",
                queue, count, first, buffer);
        std::fflush(stderr);
    }
    const std::uint32_t message = static_cast<std::uint32_t>(MEM_W(first * 4, n64_address(buffer)));
    if (si_completion_queue) {
        std::fprintf(stderr, "RW017_SI_QUEUE_MESSAGE queue=0x%08X message=0x%08X\n", queue, message);
        std::fflush(stderr);
    }
    const std::int32_t valid_before = static_cast<std::int32_t>(MEM_W(0x08, queue_memory));
    if (output != 0) {
        MEM_W(0, n64_address(output)) = static_cast<std::int32_t>(message);
        if (si_completion_queue) {
            std::fprintf(stderr, "RW017_SI_OUTPUT queue=0x%08X output=0x%08X\n", queue, output);
            std::fflush(stderr);
        }
    }
    MEM_W(0x0C, queue_memory) = (first + 1) % count;
    if (si_completion_queue) {
        std::fprintf(stderr, "RW017_SI_QUEUE_FIRST queue=0x%08X first=%d\n", queue,
                static_cast<int>(MEM_W(0x0C, queue_memory)));
        std::fflush(stderr);
    }
    MEM_W(0x08, queue_memory) = static_cast<std::int32_t>(MEM_W(0x08, queue_memory)) - 1;
    if (queue == 0x80140968U && (message == 0x29BU || message == 0x29CU)) {
        const char *event = message == 0x29BU ? "sp" : "dp";
        std::uint64_t task_number = 0;
        std::uint32_t task_address = 0;
        std::uint32_t display_list = 0;
        std::uint64_t delivery_retrace = 0;
        std::uint32_t producer_return = 0;
        for (auto it = g_graphics_completions.begin();
                it != g_graphics_completions.end(); ++it) {
            if (it->message != message) continue;
            task_number = it->task_number;
            task_address = it->task_address;
            display_list = it->display_list;
            delivery_retrace = it->vi_retrace;
            producer_return = it->producer_return;
            g_graphics_completions.erase(it);
            break;
        }
        std::fprintf(stderr,
                "RW023_GUEST_EVENT_RECEIVE event=%s task=%llu "
                "task_guest=0x%08X dl_guest=0x%08X delivery_retrace=%llu "
                "retrace_now=%llu queue=0x%08X msg=0x%08X "
                "resumed_thread=0x%08X resumed_entry=0x%08X "
                "resume_return=0x%08X producer_return=0x%08X valid_before=%d\n",
                event, static_cast<unsigned long long>(task_number), task_address,
                display_list, static_cast<unsigned long long>(delivery_retrace),
                static_cast<unsigned long long>(g_vi_29a_enqueued.load()), queue,
                message, g_host_thread, g_host_entry,
                static_cast<unsigned>(ctx->r31), producer_return, valid_before);
        std::fflush(stderr);
    }
    if (si_completion_queue) {
        std::fprintf(stderr, "RW017_SI_QUEUE_VALID queue=0x%08X valid=%d\n", queue,
                static_cast<int>(MEM_W(0x08, queue_memory)));
        std::fflush(stderr);
    }
    if (queue == 0x800EDBD8U || queue == 0x80109C00U) {
        const std::uint32_t payload_word0 = message >= 0x80000000U &&
                message < xr64::rage_wars::memory_profile::kKseg0End
                ? static_cast<std::uint32_t>(MEM_W(0, n64_address(message))) : 0U;
        std::fprintf(stderr,
                "RW017_SI_RECV queue=0x%08X message=0x%08X payload_word0=0x%08X valid_before=%d\n",
                queue, message, payload_word0, valid_before);
        std::fflush(stderr);
    }
    if (queue == 0x80140968U && message == 0x29AU) {
        ++g_vi_29a_received;
        std::fprintf(stderr, "G14 vi_receive thread=0x%08X entry=0x%08X queue=0x%08X msg=0x%08X valid_before=%d\n",
                g_host_thread, g_host_entry, queue, message, valid_before);
        std::fflush(stderr);
    }
    if (main_frame_queue && message == kSyntheticRetraceMessage) ++g_synthetic_retraces_consumed;
    ctx->r2 = 0;
    lock.unlock();
    g_host_cv.notify_all();
}

void gate7_func_002BB350(std::uint8_t *rdram, recomp_context *ctx) {
    const std::uint32_t queue = static_cast<std::uint32_t>(ctx->r4);
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    if (std::getenv("XR64_RW021_SENDER_TRACE") != nullptr && queue == 0x800FF3E8U) {
        const std::uint32_t item = static_cast<std::uint32_t>(ctx->r5);
        std::fprintf(stderr,
                "RW021_WORK_SEND current_thread=0x%08X entry=0x%08X item=0x%08X word0=0x%08X "
                "word1=0x%08X word2=0x%08X word3=0x%08X ra=0x%08X sp=0x%08X block=%d\n",
                static_cast<unsigned>(ultramodern::this_thread()),
                static_cast<unsigned>(recomp::get_current_thread_entry_address()), item,
                static_cast<unsigned>(MEM_W(0, n64_address(item))), static_cast<unsigned>(MEM_W(4, n64_address(item))),
                static_cast<unsigned>(MEM_W(8, n64_address(item))), static_cast<unsigned>(MEM_W(12, n64_address(item))),
                static_cast<unsigned>(ctx->r31), static_cast<unsigned>(ctx->r29), static_cast<int>(ctx->r6));
        std::fflush(stderr);
    }
#endif
    osSendMesg_recomp(rdram, ctx);
    return;
#endif
    const std::uint32_t message = static_cast<std::uint32_t>(ctx->r5);
    const bool block = static_cast<std::int32_t>(ctx->r6) != 0;
    std::unique_lock<std::mutex> lock(g_host_mutex);
    std::fprintf(stderr, "G14 send entry=0x%08X queue=0x%08X msg=0x%08X flags=%d\n", g_host_entry, queue, message, block ? 1 : 0);
    std::fflush(stderr);
    ++g_host_queue_sends;
    while (!enqueue_host_message(rdram, queue, message)) {
        if (!block) {
            ctx->r2 = static_cast<gpr>(-1);
            return;
        }
        g_host_cv.wait(lock, [&] {
            const gpr queue_memory = n64_address(queue);
            return g_host_stop || static_cast<std::int32_t>(MEM_W(0x08, queue_memory)) <
                    static_cast<std::int32_t>(MEM_W(0x10, queue_memory));
        });
        if (g_host_stop) {
            lock.unlock();
            exit_host_thread();
        }
        if (g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::graphics_task_capture) {
            lock.unlock();
            stop_resident(g_runtime.boundary, g_runtime.boundary_address);
        }
    }
    if (queue == 0x801409A0U) {
        std::uint32_t generated_task = 0;
        const std::uint32_t message_physical = message & 0x007FFFFFU;
        if (message >= 0x80000000U && message < xr64::rage_wars::memory_profile::kKseg0End &&
                message_physical <= kRdramSize - 0x14U) {
            generated_task = message + 0x10U;
        }
        const std::uint64_t generation = ++g_graphics_task_generations;
        std::fprintf(stderr,
                "RW023_TASK_GENERATED generation=%llu retrace=%llu "
                "node=0x%08X task_guest=0x%08X producer_thread=0x%08X "
                "producer_entry=0x%08X producer_return=0x%08X\n",
                static_cast<unsigned long long>(generation),
                static_cast<unsigned long long>(g_vi_29a_enqueued.load()),
                message, generated_task, g_host_thread, g_host_entry,
                static_cast<unsigned>(ctx->r31));
        std::fflush(stderr);
    }
    ctx->r2 = 0;
    lock.unlock();
    g_host_cv.notify_all();
}
void resident_wave11_func_002BB480(std::uint8_t *rdram, recomp_context *ctx) {
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    osSetEventMesg_recomp(rdram, ctx);
    return;
#else
    // Preserve the original libultra event table for the standalone harness;
    // Gate 6 registers events with N64ModernRuntime's host event dispatcher.
    const gpr event_entry = n64_address(0x80147B10U + static_cast<std::uint32_t>(ctx->r4) * 8U);
    MEM_W(0x00, event_entry) = ctx->r5;
    MEM_W(0x04, event_entry) = ctx->r6;
    if (static_cast<std::uint32_t>(ctx->r4) == 5U) {
        std::lock_guard<std::mutex> lock(g_host_mutex);
        g_si_event_rdram = rdram;
        g_si_event_queue = static_cast<std::uint32_t>(ctx->r5);
        g_si_event_message = static_cast<std::uint32_t>(ctx->r6);
        std::fprintf(stderr, "RW017_SI_REGISTER event=5 queue=0x%08X msg=0x%08X\n",
                g_si_event_queue, g_si_event_message);
        std::fflush(stderr);
    }
#endif
}

void resident_wave9_func_002BAB90(std::uint8_t *rdram, recomp_context *ctx) {
    const std::uint32_t queue = static_cast<std::uint32_t>(ctx->r4);
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    osCreateMesgQueue_recomp(rdram, ctx);
    return;
#else
    // Preserve the ROM's original libultra sentinel layout for the standalone
    // archaeology harness; Gate 6 uses N64ModernRuntime's null-head layout.
    const gpr sentinel = n64_address(0x800D3F10U);
    MEM_W(0x00, ctx->r4) = sentinel;
    MEM_W(0x04, ctx->r4) = sentinel;
    MEM_W(0x08, ctx->r4) = 0;
    MEM_W(0x0C, ctx->r4) = 0;
    MEM_W(0x10, ctx->r4) = ctx->r6;
    MEM_W(0x14, ctx->r4) = ctx->r5;
#endif
}

void resident_wave11_func_002BB090(std::uint8_t *rdram, recomp_context *ctx) {
    const std::uint32_t queue = static_cast<std::uint32_t>(ctx->r4);
#if defined(XR64_RAGE_WARS_USE_N64MODERNRUNTIME)
    osJamMesg_recomp(rdram, ctx);
    return;
#endif
    const std::uint32_t message = static_cast<std::uint32_t>(ctx->r5);
    const bool block = static_cast<std::int32_t>(ctx->r6) != 0;
    std::unique_lock<std::mutex> lock(g_host_mutex);
    while (!jam_host_message(rdram, queue, message)) {
        if (!block) {
            ctx->r2 = static_cast<gpr>(-1);
            return;
        }
        g_host_cv.wait(lock, [&] {
            const gpr queue_memory = n64_address(queue);
            return g_host_stop || static_cast<std::int32_t>(MEM_W(0x08, queue_memory)) <
                    static_cast<std::int32_t>(MEM_W(0x10, queue_memory));
        });
        if (g_host_stop) {
            lock.unlock();
            exit_host_thread();
        }
        if (g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::graphics_task_capture) {
            lock.unlock();
            stop_resident(g_runtime.boundary, g_runtime.boundary_address);
        }
    }
    ++g_host_queue_sends;
    ctx->r2 = 0;
    lock.unlock();
    g_host_cv.notify_all();
}


#define XR64_VIRTUAL_BOUNDARY(symbol, address) \
    void resident_wave14_external_##symbol(std::uint8_t *, recomp_context *) { \
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, address); \
    }

#define XR64_VIRTUAL_BOUNDARY_WAVE15(symbol, address) \
    void resident_wave15_external_##symbol(std::uint8_t *, recomp_context *) { \
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, address); \
    }

XR64_VIRTUAL_BOUNDARY(004009F4, 0x004009F4U)
XR64_VIRTUAL_BOUNDARY(0040184C, 0x0040184CU)
XR64_VIRTUAL_BOUNDARY(00401980, 0x00401980U)
XR64_VIRTUAL_BOUNDARY(004030E0, 0x004030E0U)
XR64_VIRTUAL_BOUNDARY(00403458, 0x00403458U)
XR64_VIRTUAL_BOUNDARY(004037D4, 0x004037D4U)
XR64_VIRTUAL_BOUNDARY(004037E8, 0x004037E8U)
XR64_VIRTUAL_BOUNDARY(00404D84, 0x00404D84U)
XR64_VIRTUAL_BOUNDARY(00405CA8, 0x00405CA8U)
XR64_VIRTUAL_BOUNDARY(0040C11C, 0x0040C11CU)
XR64_VIRTUAL_BOUNDARY(0040E9A8, 0x0040E9A8U)
XR64_VIRTUAL_BOUNDARY(0040ECB0, 0x0040ECB0U)
XR64_VIRTUAL_BOUNDARY(0040F5B4, 0x0040F5B4U)
XR64_VIRTUAL_BOUNDARY(0041174C, 0x0041174CU)
XR64_VIRTUAL_BOUNDARY(00411E70, 0x00411E70U)
XR64_VIRTUAL_BOUNDARY(00411E98, 0x00411E98U)
XR64_VIRTUAL_BOUNDARY(00411F6C, 0x00411F6CU)
XR64_VIRTUAL_BOUNDARY(004143E0, 0x004143E0U)
XR64_VIRTUAL_BOUNDARY(00414430, 0x00414430U)
XR64_VIRTUAL_BOUNDARY(00414D4C, 0x00414D4CU)
XR64_VIRTUAL_BOUNDARY(0041991C, 0x0041991CU)
XR64_VIRTUAL_BOUNDARY(00419EA4, 0x00419EA4U)
XR64_VIRTUAL_BOUNDARY(00444260, 0x00444260U)
XR64_VIRTUAL_BOUNDARY(0041A480, 0x0041A480U)
XR64_VIRTUAL_BOUNDARY(0041A5D0, 0x0041A5D0U)
XR64_VIRTUAL_BOUNDARY(0041AC10, 0x0041AC10U)
XR64_VIRTUAL_BOUNDARY(0041B160, 0x0041B160U)
XR64_VIRTUAL_BOUNDARY(0041B660, 0x0041B660U)
XR64_VIRTUAL_BOUNDARY(0041EAE0, 0x0041EAE0U)
XR64_VIRTUAL_BOUNDARY(0041F1B0, 0x0041F1B0U)
XR64_VIRTUAL_BOUNDARY_WAVE15(00441384, 0x00441384U)
XR64_VIRTUAL_BOUNDARY(00442B98, 0x00442B98U)
XR64_VIRTUAL_BOUNDARY(00444030, 0x00444030U)
XR64_VIRTUAL_BOUNDARY(00446EF0, 0x00446EF0U)
XR64_VIRTUAL_BOUNDARY_WAVE15(002097E8, 0x002097E8U)
XR64_VIRTUAL_BOUNDARY_WAVE15(0021A78C, 0x0021A78CU)
XR64_VIRTUAL_BOUNDARY_WAVE15(0022692C, 0x0022692CU)
XR64_VIRTUAL_BOUNDARY_WAVE15(002A6E98, 0x002A6E98U)
XR64_VIRTUAL_BOUNDARY_WAVE15(00449064, 0x00449064U)
XR64_VIRTUAL_BOUNDARY_WAVE15(00220A5C, 0x00220A5CU)
XR64_VIRTUAL_BOUNDARY_WAVE15(00264B1C, 0x00264B1CU)
XR64_VIRTUAL_BOUNDARY_WAVE15(00264B2C, 0x00264B2CU)
XR64_VIRTUAL_BOUNDARY_WAVE15(00449358, 0x00449358U)
XR64_VIRTUAL_BOUNDARY(0044A1EC, 0x0044A1ECU)
XR64_VIRTUAL_BOUNDARY(0044AFE0, 0x0044AFE0U)
XR64_VIRTUAL_BOUNDARY_WAVE15(002AD870, 0x002AD870U)
XR64_VIRTUAL_BOUNDARY(0044C100, 0x0044C100U)
XR64_VIRTUAL_BOUNDARY(0044D558, 0x0044D558U)
XR64_VIRTUAL_BOUNDARY(0044D904, 0x0044D904U)
XR64_VIRTUAL_BOUNDARY(0044DDD0, 0x0044DDD0U)
XR64_VIRTUAL_BOUNDARY(0044E1D0, 0x0044E1D0U)

XR64_VIRTUAL_BOUNDARY_WAVE15(00246680, 0x00246680U)
XR64_VIRTUAL_BOUNDARY_WAVE15(00448E90, 0x00448E90U)
XR64_VIRTUAL_BOUNDARY_WAVE15(00246BC8, 0x00246BC8U)
#undef XR64_VIRTUAL_BOUNDARY
#undef XR64_VIRTUAL_BOUNDARY_WAVE15

#define XR64_TRACE_FALSE_POSITIVE_BOUNDARY(symbol, address) \
    void symbol(std::uint8_t *, recomp_context *) { \
        stop_resident(xr64::rage_wars::gate12::BoundaryKind::missing_lookup, address); \
    }

// 0x00426270 recovered from live DMA ROM 0x0018A270; generated body owns it.
// Legacy trace symbol had the wrong ROM mapping; delegate to the verified body.
void trace_func_0042D190_r0022DD90(std::uint8_t *rdram, recomp_context *ctx) {
    trace_func_0042D190_r00191190(rdram, ctx);
}
// 0x00438BA4 recovered from live DMA ROM 0x0019CBA4; generated body owns it.
void trace_func_002BD080_r000BDC80(std::uint8_t *rdram, recomp_context *ctx) {
    // 0x002BD080 is `mtc0 $a0, $11` (CP0 Compare).  The donor runtime's
    // guest timer list is serviced by the native compare owner; preserve the
    // write as a host deadline instead of dropping the producer contract.
    // The standalone checkpoint starts this owner before entering generated
    // code. Gate5 reaches the same ABI hook through N64ModernRuntime, so make
    // the shared boundary responsible for ensuring the producer exists.
    g_guest_timer_producer.start(rdram);
    g_guest_timer_producer.arm(static_cast<std::uint32_t>(ctx->r4));
}
#undef XR64_TRACE_FALSE_POSITIVE_BOUNDARY

}  // extern "C"

bool xr64::rage_wars::recomp::initialize_gate5_desktop(std::string& error) {
    return g_desktop_renderer.initialize(error);
}
bool xr64::rage_wars::recomp::initialize_gate5_desktop(std::string& error, bool attempt_xr) {
    return g_desktop_renderer.initialize(error, attempt_xr);
}

void xr64::rage_wars::recomp::request_gate5_xr_transition(
        xr64::rage_wars::recomp::XrTransitionRequest request) {
    g_desktop_renderer.request_xr_transition(request);
}

xr64::rage_wars::recomp::XrPresentationMode
xr64::rage_wars::recomp::gate5_xr_presentation_mode() {
    return g_desktop_renderer.xr_presentation_mode();
}

std::uint64_t xr64::rage_wars::recomp::gate5_xr_transition_revision() {
    return g_desktop_renderer.xr_transition_revision();
}

std::string xr64::rage_wars::recomp::gate5_xr_transition_status() {
    return g_desktop_renderer.xr_transition_status();
}

bool xr64::rage_wars::recomp::gate5_independent_presentation() {
    return g_desktop_renderer.independent_presentation();
}
bool xr64::rage_wars::recomp::present_gate5_desktop(std::string& error) {
    return g_desktop_renderer.present_latest(error);
}

bool xr64::rage_wars::recomp::pump_gate5_desktop_events() {
    return g_desktop_renderer.pump_events();
}

xr64::rage_wars::recomp::DesktopControllerSnapshot
xr64::rage_wars::recomp::gate5_desktop_controller_snapshot() {
    return g_desktop_renderer.controller_snapshot();
}

xr64::rage_wars::recomp::DesktopControllerSnapshot
xr64::rage_wars::recomp::consume_gate5_desktop_controller_snapshot(bool gameplay, bool xr_gameplay) {
    return g_desktop_renderer.consume_controller_snapshot(gameplay, xr_gameplay);
}

xr64::rage_wars::controls::LookSample
xr64::rage_wars::recomp::consume_gate5_desktop_look() {
    return g_desktop_renderer.consume_look_sample();
}

std::uint8_t xr64::rage_wars::recomp::consume_gate5_desktop_weapon_cycle() {
    return g_desktop_renderer.consume_weapon_cycle();
}

void xr64::rage_wars::recomp::shutdown_gate5_desktop() {
    g_desktop_renderer.shutdown();
}

xr64::rage_wars::recomp::CheckpointResult xr64::rage_wars::recomp::run_boot_checkpoint(
        const std::filesystem::path &rom_path,
        const std::filesystem::path &type1d_data_path) {
    CheckpointResult result;
#if defined(_WIN32)
    static const bool exception_handler_installed =
            AddVectoredExceptionHandler(1, gate14_exception_trace) != nullptr;
    (void)exception_handler_installed;
#endif

    try {
    reset_checkpoint_state();
    std::string renderer_error;
    if (!g_desktop_renderer.initialize(renderer_error)) {
        fail("RW012 renderer initialization failed: " + renderer_error);
    }
    std::fprintf(stderr, "RW012_RENDERER_READY backend=PerfectDark-style-SDL2-OpenGL\n");
    std::fflush(stderr);
    std::vector<std::uint8_t> rom = read_rom(rom_path);
    ::rage_wars::install_asset_loader_service();
    ::rage_wars::asset_loader().bind_cart_rom(rom);
    const auto type1d_descriptors = xr64::rage_wars::type1d::load_private_data(
            resolve_type1d_data_path(rom_path, type1d_data_path));
    std::vector<std::uint8_t> rdram_storage(kRdramSize, 0);
    initial_dma(rom, rdram_storage);
    std::uint8_t *rdram = rdram_storage.data();
    publish_type1d_data(rdram, type1d_descriptors);

    // The standalone checkpoint retains RW005 bootstrap ownership.  Publish
    // the one proven ROM-backed KUSEG data page used by the startup table.
    map_rom_vm_page(rom, 0x0044A000U, 0x001AE000U, 0x1000U);

    rage_wars_register_generated_overlays();
    ::recomp::overlays::init_overlays();
    load_overlays(kInitialRomOffset,
            static_cast<std::int32_t>(0x00200400U), kInitialDmaSize);

    const std::size_t function_count = validate_function_table();
    g_rom = &rom;
#if defined(PCRAGE_RECOVERY_ASSET_RESOLVER)
    pcrage_initialize_recovery_asset();
#endif
    require_lookup(kMainAddress);

    // Historical startup configured the native controller service before
    // guest SI activity.  Keep the proven completion producer enabled in the
    // fresh host; the optional front-end callbacks remain absent here because
    // this checkpoint has no controller/Pak UI state to own.
    ::rage_wars::controller_service().configure({
            fresh_controller_port_status, fresh_controller_input,
            nullptr, nullptr, signal_controller_completion});
    std::fprintf(stderr, "RW017_SI_OWNER start=controller-service\n");
    std::fflush(stderr);

    recomp_context boot_context{};
    xr64::rage_wars::gate12::initialize(g_runtime, rdram, boot_context);
    ::rage_wars::register_native_resident_callable(
            static_cast<std::int32_t>(kMainAddress), boot_boundary_main);
    ::rage_wars::install_code_residency_service();
    ::recomp::set_break_handler(rage_wars_break_handler);
    g_intercept_main = true;
    recomp_entrypoint(rdram, &boot_context);
    g_intercept_main = false;
    if (!g_main_boundary_reached) fail("generated entrypoint did not reach resident main");
    if (!g_entrypoint_break_reached) fail("generated entrypoint did not reach expected break");
    if (static_cast<std::uint32_t>(boot_context.r29) != 0x803FFFC0U) {
        fail("generated entrypoint stack pointer mismatch");
    }
    if (MEM_W(0x10, boot_context.r29) != 0x00100000U ||
            MEM_W(0x14, boot_context.r29) != 0x00000007U) {
        fail("generated entrypoint stack arguments mismatch");
    }
    const std::uint32_t boot_stack_pointer = static_cast<std::uint32_t>(boot_context.r29);

    if (setjmp(g_resident_boundary_jump) == 0) {
        std::fprintf(stderr, "G14 resident_main_enter\n");
        std::fflush(stderr);
        g_resident_running = true;
        g_guest_timer_producer.start(rdram);
        ultramodern::set_pause_handler(rage_wars_pause_handler);
        ultramodern::events::set_callbacks({checkpoint_vi_tick, nullptr});
        ultramodern::set_vi_message_callback(checkpoint_vi_message, rdram);
        rw_main(rdram, &boot_context);
        {
            std::unique_lock<std::mutex> lock(g_host_mutex);
            g_runtime.boundary = xr64::rage_wars::gate12::BoundaryKind::resident_main_return;
            g_host_cv.wait_for(lock, std::chrono::seconds(45), [] {
                return g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::graphics_task_capture ||
                        (g_runtime.boundary != xr64::rage_wars::gate12::BoundaryKind::none &&
                         g_runtime.boundary != xr64::rage_wars::gate12::BoundaryKind::resident_main_return);
            });
            g_host_stop = true;
        }
        g_host_cv.notify_all();
        g_resident_running = false;
    }
    ultramodern::join_vi_thread();
    ultramodern::set_vi_message_callback(nullptr, nullptr);
    stop_and_join_host_workers();
    ::recomp::set_break_handler(nullptr);
    ultramodern::set_pause_handler(nullptr);
    g_desktop_renderer.shutdown();
    {
        std::lock_guard<std::mutex> lock(g_host_mutex);
        if (!g_async_error.empty()) fail(g_async_error);
    }
    if (g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::none) {
        fail("resident main stopped without a classified runtime boundary");
    }

    std::array<char, 2048> summary{};
    std::snprintf(summary.data(), summary.size(),
            "XR64_RECOMP_GATE14_OK sections=%zu table_sections=%zu generated_functions=%zu "
            "rdram_bytes=%zu initial_dma_bytes=%zu boot_boundary=resident_main "
            "boot_sp=0x%08X resident_sp=0x%08X resident_boundary=%s resident_boundary_address=0x%08X "
            "runtime_service_calls=%llu interrupt_disable_calls=%llu interrupt_restore_calls=%llu "
            "cache_calls=%llu tlb_calls=%llu count_calls=%llu si_status_calls=%llu "
            "scheduler_dispatch_calls=%llu scheduler_yield_calls=%llu dispatched_thread=0x%08X dispatched_entry=0x%08X "
            "host_threads_created=%llu host_threads_started=%llu host_queue_receives=%llu host_queue_sends=%llu "
            "main_frame_wait_calls=%llu synthetic_retraces=%llu synthetic_retraces_consumed=%llu input_contract_frames=%zu "
            "input_contract_hash=0x%016llX vi_ticks=%llu "
            "vi_29a_enqueued=%llu vi_29a_received=%llu code_residency_requests=%llu "
            "code_page_materializations=%llu missing_lookup=0x%08X",
            num_sections, kSectionTableEntryCount, function_count, rdram_storage.size(), kInitialDmaSize,
            boot_stack_pointer, static_cast<std::uint32_t>(boot_context.r29),
            xr64::rage_wars::gate12::boundary_name(g_runtime.boundary), g_runtime.boundary_address,
            static_cast<unsigned long long>(g_runtime.service_calls),
            static_cast<unsigned long long>(g_runtime.interrupt_disable_calls),
            static_cast<unsigned long long>(g_runtime.interrupt_restore_calls),
            static_cast<unsigned long long>(g_runtime.cache_calls),
            static_cast<unsigned long long>(g_runtime.tlb_calls),
            static_cast<unsigned long long>(g_runtime.count_calls),
            static_cast<unsigned long long>(g_runtime.si_status_calls),
            static_cast<unsigned long long>(g_runtime.scheduler_dispatch_calls),
            static_cast<unsigned long long>(g_runtime.scheduler_yield_calls),
            g_runtime.dispatched_thread, g_runtime.dispatched_entry,
            static_cast<unsigned long long>(g_host_threads_created),
            static_cast<unsigned long long>(g_host_threads_started),
            static_cast<unsigned long long>(g_host_queue_receives),
            static_cast<unsigned long long>(g_host_queue_sends),
            static_cast<unsigned long long>(g_main_frame_wait_calls),
            static_cast<unsigned long long>(g_synthetic_retraces),
            static_cast<unsigned long long>(g_synthetic_retraces_consumed),
            xr64::rage_wars::input_frame_count(),
            static_cast<unsigned long long>(xr64::rage_wars::input_script_hash()),
            static_cast<unsigned long long>(g_vi_ticks.load()),
            static_cast<unsigned long long>(g_vi_29a_enqueued.load()),
            static_cast<unsigned long long>(g_vi_29a_received.load()),
            static_cast<unsigned long long>(g_code_residency_requests.load()),
            static_cast<unsigned long long>(g_code_page_materializations), g_missing_lookup);
    std::fflush(stderr);
    result.ok = true;
    result.summary = summary.data();
    result.generated_function_count = function_count;
    result.boundary = xr64::rage_wars::gate12::boundary_name(g_runtime.boundary);
    result.boundary_address = g_runtime.boundary_address;
    if (g_runtime.boundary == xr64::rage_wars::gate12::BoundaryKind::graphics_task_capture) {
        std::lock_guard<std::mutex> lock(g_graphics_capture_mutex);
        result.graphics_ostask = g_graphics_capture.ostask;
        result.graphics_rdram = g_graphics_capture.canonical_rdram;
    }
    return result;
    } catch (const std::exception &exception) {
        stop_and_join_host_workers();
        ultramodern::set_pause_handler(nullptr);
        g_desktop_renderer.shutdown();
        result.error = exception.what();
        return result;
    }
}

extern "C" int xr64_audio_queue_guest(std::uint8_t* rdram,recomp_context* ctx) {
    if(!xr64::rage_wars::audio::enabled())return 0;
    const auto address=static_cast<std::uint32_t>(ctx->r4)&0x1FFFFFFFU;
    const auto count=static_cast<std::uint32_t>(ctx->r5);
    if(address>kRdramSize || count>kRdramSize-address || (count&3U)) {ctx->r2=static_cast<gpr>(-1);return 1;}
    xr64::rage_wars::audio::begin_submission();
    ultramodern::queue_audio_buffer(rdram,static_cast<std::uint32_t>(ctx->r4),count);
    ctx->r2=xr64::rage_wars::audio::submission_succeeded()?0:static_cast<gpr>(-1);return 1;
}

// Full-body prototype: default off; no simulation or inventory overrides.
#include "rage_wars_body_policy.h"
#include "rage_wars_body_toggle.hpp"
namespace {
RageWarsBodyToggle& body_toggle() {
    static RageWarsBodyToggle state([] { const char* value = std::getenv("XR64_RW_FULL_BODY"); return value && std::strcmp(value, "1") == 0; }());
    return state;
}
}
extern "C" void xr64_body_request_toggle() {
    body_toggle().request();
    std::fprintf(stderr, "RW_BODY_TOGGLE requested=1 source=ctrl-b\n");
}
extern "C" void xr64_body_begin_view() {
    if (body_toggle().commit()) {
        std::fprintf(stderr, "RW_BODY_TOGGLE enabled=%d boundary=world-pass head_filter=unimplemented\n", body_toggle().enabled() ? 1 : 0);
    }
}
extern "C" int xr64_body_local_view_enabled(uint8_t* rdram, gpr actor, gpr view) {
    const int local = xr64_body_local_view(rdram, (uint32_t)actor, (uint32_t)view, body_toggle().enabled());
    if (local) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true)) std::fprintf(stderr, "RW_BODY_PROTOTYPE local_actor=0x%08X view=0x%08X head_filter=unimplemented\n", (uint32_t)actor, (uint32_t)view);
    }
    return local;
}
