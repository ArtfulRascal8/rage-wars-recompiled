#pragma once

#include "recomp.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace xr64::rage_wars::gate12 {

enum class BoundaryKind {
    none,
    scheduler_enqueue_and_yield,
    scheduler_dispatch,
    missing_lookup,
    pause_self,
    mips_break,
    syscall,
    graphics_task_capture,
    resident_main_return,
};

struct TlbEntry {
    bool valid = false;
    std::uint32_t page_mask = 0;
    std::uint32_t entry_hi = 0;
    std::uint32_t physical_even = 0xFFFFFFFFU;
    std::uint32_t physical_odd = 0xFFFFFFFFU;
    std::uint32_t flags = 0;
};

struct RuntimeState {
    BoundaryKind boundary = BoundaryKind::none;
    std::uint32_t boundary_address = 0;
    std::uint64_t service_calls = 0;
    std::uint64_t interrupt_disable_calls = 0;
    std::uint64_t interrupt_restore_calls = 0;
    std::uint64_t cache_calls = 0;
    std::uint64_t tlb_calls = 0;
    std::uint64_t count_calls = 0;
    std::uint64_t si_status_calls = 0;
    std::uint64_t scheduler_dispatch_calls = 0;
    std::uint64_t scheduler_yield_calls = 0;
    std::uint32_t deterministic_count = 0;
    std::uint32_t dispatched_thread = 0;
    std::uint32_t dispatched_entry = 0;
    std::array<TlbEntry, 32> tlb{};
};

void initialize(RuntimeState &state, std::uint8_t *rdram, recomp_context &ctx);
const char *boundary_name(BoundaryKind boundary);

void map_tlb(RuntimeState &state, std::uint8_t *rdram, recomp_context &ctx);
void unmap_tlb(RuntimeState &state, recomp_context &ctx);
void unmap_tlb_all(RuntimeState &state);
void probe_tlb(RuntimeState &state, recomp_context &ctx);
void get_count(RuntimeState &state, recomp_context &ctx);
void disable_interrupts(RuntimeState &state, recomp_context &ctx);
void restore_interrupts(RuntimeState &state, recomp_context &ctx);
void set_interrupt_mask(RuntimeState &state, recomp_context &ctx);
void cache_operation(RuntimeState &state);
void si_device_busy(RuntimeState &state, recomp_context &ctx);

}  // namespace xr64::rage_wars::gate12
