#include "runtime_boundary.hpp"
#include "rdram_profile.hpp"

#include <librecomp/overlays.hpp>

#include <cstdio>

namespace xr64::rage_wars::gate12 {

namespace {
constexpr std::uint32_t kInvalidPhysical = 0xFFFFFFFFU;
std::uint32_t page_half_size(std::uint32_t page_mask) {
    return (page_mask + 0x2000U) >> 1U;
}
void unregister_tlb_entry(const TlbEntry &entry) {
    if (!entry.valid) return;
    const std::uint32_t half_size = page_half_size(entry.page_mask);
    const std::uint32_t pair_size = half_size * 2U;
    const std::uint32_t pair_base = entry.entry_hi & ~(pair_size - 1U);
    recomp::overlays::unregister_tlb_mapping(pair_base, pair_size);
}

void register_tlb_entry(const TlbEntry &entry) {
    if (!entry.valid) return;
    const std::uint32_t half_size = page_half_size(entry.page_mask);
    const std::uint32_t pair_size = half_size * 2U;
    const std::uint32_t pair_base = entry.entry_hi & ~(pair_size - 1U);
    if (entry.physical_even != kInvalidPhysical) {
        recomp::overlays::register_tlb_mapping(
                pair_base, entry.physical_even, half_size);
    }
    if (entry.physical_odd != kInvalidPhysical) {
        recomp::overlays::register_tlb_mapping(
                pair_base + half_size, entry.physical_odd, half_size);
    }
}}  // namespace

void initialize(RuntimeState &state, std::uint8_t *rdram, recomp_context &ctx) {
    state = RuntimeState{};
    ctx.f_odd = &ctx.f1.u32l;
    ctx.mips3_float_mode = true;

    // IPL3-owned globals initialized by N64ModernRuntime before the entrypoint.
    constexpr std::int32_t os_tv_type = static_cast<std::int32_t>(0x80000300U);
    constexpr std::int32_t os_rom_base = static_cast<std::int32_t>(0x80000308U);
    constexpr std::int32_t os_reset_type = static_cast<std::int32_t>(0x8000030CU);
    constexpr std::int32_t os_mem_size = static_cast<std::int32_t>(0x80000318U);
    MEM_W(os_tv_type, 0) = 1;
    MEM_W(os_rom_base, 0) = static_cast<std::int32_t>(0xB0000000U);
    MEM_W(os_reset_type, 0) = 0;
    MEM_W(os_mem_size, 0) = static_cast<std::int32_t>(memory_profile::kRdramSize);
}

const char *boundary_name(BoundaryKind boundary) {
    switch (boundary) {
        case BoundaryKind::none: return "none";
        case BoundaryKind::scheduler_enqueue_and_yield: return "scheduler_enqueue_and_yield";
        case BoundaryKind::scheduler_dispatch: return "scheduler_dispatch";
        case BoundaryKind::missing_lookup: return "missing_lookup";
        case BoundaryKind::pause_self: return "pause_self";
        case BoundaryKind::mips_break: return "mips_break";
        case BoundaryKind::syscall: return "syscall";
        case BoundaryKind::graphics_task_capture: return "graphics_task_capture";
        case BoundaryKind::resident_main_return: return "resident_main_return";
    }
    return "unknown";
}

void map_tlb(RuntimeState &state, std::uint8_t *rdram, recomp_context &ctx) {
    ++state.service_calls;
    ++state.tlb_calls;
    const std::uint32_t index = static_cast<std::uint32_t>(ctx.r4);
    if (index >= state.tlb.size()) return;
    TlbEntry &entry = state.tlb[index];
    unregister_tlb_entry(entry);
    entry.valid = true;
    entry.page_mask = static_cast<std::uint32_t>(ctx.r5);
    entry.entry_hi = static_cast<std::uint32_t>(ctx.r6);
    entry.physical_even = static_cast<std::uint32_t>(ctx.r7);
    entry.physical_odd = MEM_W(0x10, ctx.r29);
    entry.flags = MEM_W(0x14, ctx.r29) & 7U;
    std::fprintf(stderr,
            "G14 tlb_map index=%u mask=0x%08X hi=0x%08X even=0x%08X odd=0x%08X flags=0x%X\n",
            index, entry.page_mask, entry.entry_hi, entry.physical_even,
            entry.physical_odd, entry.flags);
    std::fflush(stderr);
    register_tlb_entry(entry);
}

void unmap_tlb(RuntimeState &state, recomp_context &ctx) {
    ++state.service_calls;
    ++state.tlb_calls;
    const std::uint32_t index = static_cast<std::uint32_t>(ctx.r4);
    if (index < state.tlb.size()) {
        unregister_tlb_entry(state.tlb[index]);
        state.tlb[index] = TlbEntry{};
    }
}

void unmap_tlb_all(RuntimeState &state) {
    ++state.service_calls;
    ++state.tlb_calls;
    recomp::overlays::unregister_all_tlb_mappings();
    for (std::size_t index = 0; index <= 30; ++index) state.tlb[index] = TlbEntry{};
}

void probe_tlb(RuntimeState &state, recomp_context &ctx) {
    ++state.service_calls;
    ++state.tlb_calls;
    const std::uint32_t address = static_cast<std::uint32_t>(ctx.r4);
    ctx.r2 = static_cast<gpr>(static_cast<std::int32_t>(-1));
    for (const TlbEntry &entry : state.tlb) {
        if (!entry.valid) continue;
        const std::uint32_t half_size = page_half_size(entry.page_mask);
        const std::uint32_t pair_size = half_size * 2U;
        const std::uint32_t pair_mask = ~(pair_size - 1U);
        if ((address & pair_mask) != (entry.entry_hi & pair_mask)) continue;
        const bool odd = (address & half_size) != 0;
        const std::uint32_t physical = odd ? entry.physical_odd : entry.physical_even;
        if (physical == kInvalidPhysical) return;
        ctx.r2 = static_cast<gpr>(physical + (address & (half_size - 1U)));
        return;
    }
}

void get_count(RuntimeState &state, recomp_context &ctx) {
    ++state.service_calls;
    ++state.count_calls;
    state.deterministic_count += 100U;
    ctx.r2 = static_cast<gpr>(state.deterministic_count);
}

void disable_interrupts(RuntimeState &state, recomp_context &ctx) {
    ++state.service_calls;
    ++state.interrupt_disable_calls;
    ctx.r2 = static_cast<gpr>(ctx.status_reg & 1U);
    ctx.status_reg &= ~1U;
}

void restore_interrupts(RuntimeState &state, recomp_context &ctx) {
    ++state.service_calls;
    ++state.interrupt_restore_calls;
    ctx.status_reg |= static_cast<std::uint32_t>(ctx.r4) & 1U;
}

void set_interrupt_mask(RuntimeState &state, recomp_context &ctx) {
    ++state.service_calls;
    const std::uint32_t old_mask = ctx.status_reg & 0xFF01U;
    const std::uint32_t requested = static_cast<std::uint32_t>(ctx.r4) & 0xFF01U;
    ctx.status_reg = (ctx.status_reg & ~0xFF01U) | requested;
    ctx.r2 = static_cast<gpr>(old_mask);
}

void cache_operation(RuntimeState &state) {
    ++state.service_calls;
    ++state.cache_calls;
    // Recompiled code uses coherent host memory; libultra cache maintenance is intentionally a no-op.
}

void si_device_busy(RuntimeState &state, recomp_context &ctx) {
    ++state.service_calls;
    ++state.si_status_calls;
    // No host SI DMA is pending in the deterministic checkpoint.
    ctx.r2 = 0;
}

}  // namespace xr64::rage_wars::gate12
