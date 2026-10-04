#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "recompiler/context.h"
#include "overlays.hpp"
#include "sections.h"

static recomp::overlays::overlay_section_table_data_t sections_info {};
static recomp::overlays::overlays_by_index_t overlays_info {};

static SectionTableEntry* patch_code_sections = nullptr;
size_t num_patch_code_sections = 0;
static std::vector<char> patch_data;

struct LoadedSection {
    int32_t loaded_ram_addr;
    size_t section_table_index;

    LoadedSection(int32_t loaded_ram_addr_, size_t section_table_index_) {
        loaded_ram_addr = loaded_ram_addr_;
        section_table_index = section_table_index_;
    }

    bool operator<(const LoadedSection& rhs) {
        return loaded_ram_addr < rhs.loaded_ram_addr;
    }
};

struct CoveredRange {
    uint32_t begin;
    uint32_t end;
};

struct PendingSectionLoad {
    size_t section_table_index;
    int32_t loaded_ram_addr;
    std::vector<CoveredRange> covered_ranges;
};

struct TlbMapping {
    uint32_t virtual_addr;
    uint32_t physical_addr;
    uint32_t size;
};

struct TlbBoundSection {
    size_t section_table_index;
    int32_t backing_ram_addr;
    int32_t virtual_ram_addr;
    uint32_t mapping_virtual_addr;
    uint32_t mapping_physical_addr;
    uint32_t mapping_size;
    std::vector<std::pair<int32_t, recomp_func_t*>> bound_functions;
};
static std::unordered_map<uint32_t, uint16_t> code_sections_by_rom{};
static std::unordered_map<uint32_t, uint16_t> patch_code_sections_by_rom{};
static std::vector<LoadedSection> loaded_sections{};
static std::vector<PendingSectionLoad> pending_section_loads{};
static std::vector<TlbMapping> active_tlb_mappings{};
static std::vector<TlbBoundSection> tlb_bound_sections{};
static std::unordered_map<int32_t, recomp_func_t*> func_map{};
// Publish the selected pointer and its ROM provenance together. Lookup diagnostics
// copy this record; they never traverse the concurrently changing overlay vectors.
static std::unordered_map<int32_t, recomp::overlays::CallableMappingIdentity> callable_identities{};
static std::mutex callable_map_mutex;

static recomp::overlays::CallableMappingIdentity mapped_callable_identity(
    const SectionTableEntry& section, size_t section_index, const FuncEntry& entry,
    int32_t loaded_addr, int32_t physical_addr, bool tlb_bound) {
    recomp::overlays::CallableMappingIdentity identity{};
    identity.active_callable = true;
    identity.tlb_bound = tlb_bound;
    identity.match_count = 1;
    identity.section_rom = section.rom_addr;
    identity.function_rom = section.rom_addr + entry.offset;
    identity.original_function_vram = section.ram_addr + entry.offset;
    identity.loaded_section_vram = static_cast<uint32_t>(loaded_addr);
    identity.physical_section_addr = static_cast<uint32_t>(physical_addr);
    identity.function_offset = entry.offset;
    identity.function_size = entry.rom_size;
    identity.logical_section_index = section.index;
    identity.code_section_index = section_index;
    return identity;
}

static void publish_callable(int32_t addr, recomp_func_t* function,
    const recomp::overlays::CallableMappingIdentity& identity = {}) {
    std::lock_guard lock(callable_map_mutex);
    func_map[addr] = function;
    callable_identities[addr] = identity;
}

static void erase_callable_if_selected(int32_t addr, recomp_func_t* function) {
    std::lock_guard lock(callable_map_mutex);
    const auto active = func_map.find(addr);
    if (active != func_map.end() && active->second == function) {
        func_map.erase(active);
        callable_identities.erase(addr);
    }
}

static void clear_callable_map() {
    std::lock_guard lock(callable_map_mutex);
    func_map.clear();
    callable_identities.clear();
}

static size_t callable_map_size() {
    std::lock_guard lock(callable_map_mutex);
    return func_map.size();
}
static std::unordered_map<std::string, recomp_func_t*> base_exports{};
static std::unordered_map<std::string, recomp_func_ext_t*> ext_base_exports{};
static std::unordered_map<std::string, size_t> base_events;
static std::unordered_map<uint32_t, recomp_func_t*> manual_patch_symbols_by_vram;
static recomp::overlays::callable_resolver_t callable_resolver = nullptr;
static recomp::overlays::callable_failure_handler_t callable_failure_handler = nullptr;

static bool rom_dma_trace_enabled() {
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    return std::getenv("XR64_RW029_OVERLAY_TRACE") != nullptr;
#else
    return false;
#endif
}

static void trace_rom_dma(const char* event, uint32_t rom, int32_t ram_addr, uint32_t size,
    size_t section_index, size_t map_size) {
    if (!rom_dma_trace_enabled()) {
        return;
    }
    std::fprintf(stderr,
        "RW029_OVERLAY_%s rom=0x%08X ram=0x%08X size=0x%08X section=%zu map_size=%zu\n",
        event, rom, static_cast<uint32_t>(ram_addr), size, section_index, map_size);
    std::fflush(stderr);
}

static bool callable_fault_trace_enabled() {
#if !defined(XR64_RAGE_WARS_CLEAN_RUN)
    return std::getenv("XR64_RW031_CALLABLE_FAULT_TRACE") != nullptr;
#else
    return false;
#endif
}

static void trace_callable_fault(const char* event, int32_t addr, bool known_generated,
    bool handler_result, recomp_func_t* func) {
    if (!callable_fault_trace_enabled()) {
        return;
    }
    std::fprintf(stderr,
        "RW031_CALLABLE_%s addr=0x%08X known_generated=%d handler_result=%d resolved=%d func=%p map_size=%zu\n",
        event, static_cast<uint32_t>(addr), known_generated ? 1 : 0,
        handler_result ? 1 : 0, func != nullptr ? 1 : 0, (void*)func, callable_map_size());
    std::fflush(stderr);
}
extern "C" {
int32_t* section_addresses = nullptr;
}

void recomp::overlays::register_overlays(const overlay_section_table_data_t& sections, const overlays_by_index_t& overlays) {
    sections_info = sections;
    overlays_info = overlays;
}

void recomp::overlays::set_callable_resolver(callable_resolver_t resolver) {
    callable_resolver = resolver;
}

void recomp::overlays::set_callable_failure_handler(callable_failure_handler_t handler) {
    callable_failure_handler = handler;
}
void recomp::overlays::register_patches(const char* patch, std::size_t size, SectionTableEntry* sections, size_t num_sections) {
    patch_code_sections = sections;
    num_patch_code_sections = num_sections;

    patch_data.resize(size);
    std::memcpy(patch_data.data(), patch, size);

    patch_code_sections_by_rom.reserve(num_patch_code_sections);
    for (size_t i = 0; i < num_patch_code_sections; i++) {
        patch_code_sections_by_rom.emplace(patch_code_sections[i].rom_addr, i);
    }
}

void recomp::overlays::register_base_export(const std::string& name, recomp_func_t* func) {
    base_exports.emplace(name, func);
}

void recomp::overlays::register_ext_base_export(const std::string& name, recomp_func_ext_t* func) {
    ext_base_exports.emplace(name, func);
}

void recomp::overlays::register_base_exports(const FunctionExport* export_list) {
    std::unordered_map<uint32_t, recomp_func_t*> patch_func_vram_map{};

    // Iterate over all patch functions to set up a mapping of their vram address.
    for (size_t patch_section_index = 0; patch_section_index < num_patch_code_sections; patch_section_index++) {
        const SectionTableEntry* cur_section = &patch_code_sections[patch_section_index];

        for (size_t func_index = 0; func_index < cur_section->num_funcs; func_index++) {
            const FuncEntry* cur_func = &cur_section->funcs[func_index];
            patch_func_vram_map.emplace(cur_section->ram_addr + cur_func->offset, cur_func->func);
        }
    }

    // Iterate over exports, using the vram mapping to create a name mapping.
    for (const FunctionExport* cur_export = &export_list[0]; cur_export->name != nullptr; cur_export++) {
        auto it = patch_func_vram_map.find(cur_export->ram_addr);
        if (it == patch_func_vram_map.end()) {
            assert(false && "Failed to find exported function in patch function sections!");
        }
        base_exports.emplace(cur_export->name, it->second);
    }
}

recomp_func_t* recomp::overlays::get_base_export(const std::string& export_name) {
    auto it = base_exports.find(export_name);
    if (it == base_exports.end()) {
        return nullptr;
    }
    return it->second;
}

recomp_func_ext_t* recomp::overlays::get_ext_base_export(const std::string& export_name) {
    auto it = ext_base_exports.find(export_name);
    if (it == ext_base_exports.end()) {
        return nullptr;
    }
    return it->second;
}

void recomp::overlays::register_base_events(char const* const* event_names) {
    for (size_t event_index = 0; event_names[event_index] != nullptr; event_index++) {
        base_events.emplace(event_names[event_index], event_index);
    }
}

size_t recomp::overlays::get_base_event_index(const std::string& event_name) {
    auto it = base_events.find(event_name);
    if (it == base_events.end()) {
        return (size_t)-1;
    }
    return it->second;
}

size_t recomp::overlays::num_base_events() {
    return base_events.size();
}

const std::unordered_map<uint32_t, uint16_t>& recomp::overlays::get_vrom_to_section_map() {
    return code_sections_by_rom;
}

uint32_t recomp::overlays::get_section_ram_addr(uint16_t code_section_index) {
    return sections_info.code_sections[code_section_index].ram_addr;
}

std::span<const RelocEntry> recomp::overlays::get_section_relocs(uint16_t code_section_index) {
    if (code_section_index < sections_info.num_code_sections) {
        const auto& section = sections_info.code_sections[code_section_index];
        return std::span{ section.relocs, section.num_relocs };
    }
    assert(false);
    return {};
}

void recomp::overlays::add_loaded_function(int32_t ram, recomp_func_t* func) {
    publish_callable(ram, func);
}

static bool mapping_ranges_overlap(uint32_t first_addr, uint32_t first_size,
    uint32_t second_addr, uint32_t second_size) {
    const uint64_t first_begin = first_addr;
    const uint64_t first_end = first_begin + first_size;
    const uint64_t second_begin = second_addr;
    const uint64_t second_end = second_begin + second_size;
    return first_begin < second_end && second_begin < first_end;
}

static void erase_tlb_bound_section(size_t binding_index) {
    const TlbBoundSection binding = tlb_bound_sections[binding_index];
    const SectionTableEntry& section = sections_info.code_sections[binding.section_table_index];
    if (callable_fault_trace_enabled()) {
        std::fprintf(stderr,
            "RW031_TLB_UNBIND virtual=0x%08X physical=0x%08X size=0x%08X section=%zu section_virtual=0x%08X section_physical=0x%08X map_size=%zu\n",
            binding.mapping_virtual_addr, binding.mapping_physical_addr, binding.mapping_size,
            binding.section_table_index, static_cast<uint32_t>(binding.virtual_ram_addr),
            static_cast<uint32_t>(binding.backing_ram_addr), callable_map_size());
        std::fflush(stderr);
    }
    for (const auto& bound_function : binding.bound_functions) {
        erase_callable_if_selected(bound_function.first, bound_function.second);
    }
    tlb_bound_sections.erase(tlb_bound_sections.begin() + binding_index);
    section_addresses[section.index] = binding.backing_ram_addr;
    for (const TlbBoundSection& remaining : tlb_bound_sections) {
        if (remaining.section_table_index == binding.section_table_index &&
            remaining.backing_ram_addr == binding.backing_ram_addr) {
            section_addresses[section.index] = remaining.virtual_ram_addr;
            break;
        }
    }
}

static void erase_tlb_bindings_for_loaded_section(
    size_t section_table_index, int32_t backing_ram_addr) {
    for (size_t binding_index = 0; binding_index < tlb_bound_sections.size();) {
        const TlbBoundSection& binding = tlb_bound_sections[binding_index];
        if (binding.section_table_index == section_table_index &&
            binding.backing_ram_addr == backing_ram_addr) {
            erase_tlb_bound_section(binding_index);
            continue;
        }
        binding_index++;
    }
}

static void bind_loaded_section_to_mapping(
    const LoadedSection& loaded, const TlbMapping& mapping) {
    const SectionTableEntry& section = sections_info.code_sections[loaded.section_table_index];
    const uint64_t section_begin = static_cast<uint32_t>(loaded.loaded_ram_addr);
    const uint64_t section_end = section_begin + section.size;
    const uint64_t mapping_begin = mapping.physical_addr;
    const uint64_t mapping_end = mapping_begin + mapping.size;
    if (section_begin >= UINT32_MAX || section_end <= mapping_begin ||
        section_begin >= mapping_end) {
        return;
    }

    const int64_t virtual_section_addr = static_cast<int64_t>(mapping.virtual_addr) +
        static_cast<int64_t>(section_begin) - static_cast<int64_t>(mapping_begin);
    if (virtual_section_addr < 0 || static_cast<uint64_t>(virtual_section_addr) > UINT32_MAX) {
        return;
    }
    const int32_t virtual_ram_addr = static_cast<int32_t>(static_cast<uint32_t>(virtual_section_addr));
    const auto existing = std::find_if(tlb_bound_sections.begin(), tlb_bound_sections.end(),
        [&loaded, mapping](const TlbBoundSection& binding) {
            return binding.section_table_index == loaded.section_table_index &&
                binding.backing_ram_addr == loaded.loaded_ram_addr &&
                binding.mapping_virtual_addr == mapping.virtual_addr &&
                binding.mapping_physical_addr == mapping.physical_addr &&
                binding.mapping_size == mapping.size;
        });
    if (existing != tlb_bound_sections.end()) {
        return;
    }

    TlbBoundSection binding{
        loaded.section_table_index,
        loaded.loaded_ram_addr,
        virtual_ram_addr,
        mapping.virtual_addr,
        mapping.physical_addr,
        mapping.size,
        {}};
    std::vector<recomp::overlays::CallableMappingIdentity> bound_identities;
    for (size_t function_index = 0; function_index < section.num_funcs; function_index++) {
        const FuncEntry& func = section.funcs[function_index];
        const uint64_t function_begin = section_begin + func.offset;
        if (function_begin < mapping_begin || function_begin >= mapping_end ||
            function_begin < section_begin || function_begin >= section_end) {
            continue;
        }
        const int64_t virtual_function_addr = static_cast<int64_t>(mapping.virtual_addr) +
            static_cast<int64_t>(function_begin) - static_cast<int64_t>(mapping_begin);
        if (virtual_function_addr < 0 || static_cast<uint64_t>(virtual_function_addr) > UINT32_MAX) {
            continue;
        }
        binding.bound_functions.emplace_back(
            static_cast<int32_t>(static_cast<uint32_t>(virtual_function_addr)), func.func);
        bound_identities.push_back(mapped_callable_identity(section,
            loaded.section_table_index, func, virtual_ram_addr,
            loaded.loaded_ram_addr, true));
    }
    if (binding.bound_functions.empty()) {
        return;
    }
    for (size_t i = 0; i < binding.bound_functions.size(); ++i) {
        const auto& bound_function = binding.bound_functions[i];
        publish_callable(bound_function.first, bound_function.second, bound_identities[i]);
    }
    tlb_bound_sections.push_back(std::move(binding));
    section_addresses[section.index] = virtual_ram_addr;
    if (callable_fault_trace_enabled()) {
        std::fprintf(stderr,
            "RW031_TLB_BIND virtual=0x%08X physical=0x%08X size=0x%08X section=%zu section_virtual=0x%08X section_physical=0x%08X map_size=%zu\n",
            mapping.virtual_addr, mapping.physical_addr, mapping.size,
            loaded.section_table_index, static_cast<uint32_t>(virtual_ram_addr),
            static_cast<uint32_t>(loaded.loaded_ram_addr), callable_map_size());
        std::fflush(stderr);
    }
}

static void bind_loaded_section_to_active_mappings(const LoadedSection& loaded) {
    for (const TlbMapping& mapping : active_tlb_mappings) {
        bind_loaded_section_to_mapping(loaded, mapping);
    }
}
static std::vector<LoadedSection>::iterator erase_loaded_section(
    std::vector<LoadedSection>::iterator loaded_it) {
    const SectionTableEntry& section = sections_info.code_sections[loaded_it->section_table_index];
    erase_tlb_bindings_for_loaded_section(
        loaded_it->section_table_index, loaded_it->loaded_ram_addr);
    for (size_t function_index = 0; function_index < section.num_funcs; function_index++) {
        const FuncEntry& func = section.funcs[function_index];
        const int32_t address = loaded_it->loaded_ram_addr + static_cast<int32_t>(func.offset);
        erase_callable_if_selected(address, func.func);
    }
    section_addresses[section.index] = section.ram_addr;
    return loaded_sections.erase(loaded_it);
}

void load_overlay(size_t section_table_index, int32_t ram) {
    const SectionTableEntry& section = sections_info.code_sections[section_table_index];

    for (size_t function_index = 0; function_index < section.num_funcs; function_index++) {
        const FuncEntry& func = section.funcs[function_index];
        publish_callable(ram + func.offset, func.func,
            mapped_callable_identity(section, section_table_index, func, ram, ram, false));
    }

    loaded_sections.emplace_back(ram, section_table_index);
    section_addresses[section.index] = ram;
    bind_loaded_section_to_active_mappings(loaded_sections.back());
}

static void load_special_overlay(const SectionTableEntry& section, int32_t ram) {
    for (size_t function_index = 0; function_index < section.num_funcs; function_index++) {
        const FuncEntry& func = section.funcs[function_index];
        // Patch/manual callables have no original-ROM mapping record.
        publish_callable(ram + func.offset, func.func);
    }
}

static void load_patch_functions() {
    if (patch_code_sections == nullptr) {
        debug_printf("[Patch] No patch section was registered\n");
        return;
    }
    for (size_t i = 0; i < num_patch_code_sections; i++) {
        load_special_overlay(patch_code_sections[i], patch_code_sections[i].ram_addr);
    }
}

void recomp::overlays::read_patch_data(uint8_t* rdram, gpr patch_data_address) {
    for (size_t i = 0; i < patch_data.size(); i++) {
        MEM_B(i, patch_data_address) = patch_data[i];
    }
}

extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size) {
    // Search for the first section that's included in the loaded rom range
    // Sections were sorted by `init_overlays` so we can use the bounds functions
    auto lower = std::lower_bound(&sections_info.code_sections[0], &sections_info.code_sections[sections_info.num_code_sections], rom,
        [](const SectionTableEntry& entry, uint32_t addr) {
            return entry.rom_addr < addr;
        }
    );
    auto upper = std::upper_bound(&sections_info.code_sections[0], &sections_info.code_sections[sections_info.num_code_sections], (uint32_t)(rom + size),
        [](uint32_t addr, const SectionTableEntry& entry) {
            return addr < entry.size + entry.rom_addr;
        }
    );
    // Load the overlays that were found
    for (auto it = lower; it != upper; ++it) {
        load_overlay(std::distance(&sections_info.code_sections[0], it), it->rom_addr - rom + ram_addr);
    }
}

static bool ranges_overlap(uint64_t first_begin, uint64_t first_end,
    uint64_t second_begin, uint64_t second_end) {
    return first_begin < second_end && second_begin < first_end;
}

static bool dma_matches_loaded_section(const SectionTableEntry& section, int32_t loaded_ram_addr,
    uint32_t rom, int32_t ram_addr) {
    return static_cast<uint32_t>(section.rom_addr - static_cast<uint32_t>(loaded_ram_addr)) ==
        static_cast<uint32_t>(rom - static_cast<uint32_t>(ram_addr));
}

static void invalidate_overwritten_sections(uint32_t rom, int32_t ram_addr, uint32_t size) {
    const uint64_t dma_begin = static_cast<uint32_t>(ram_addr);
    const uint64_t dma_end = dma_begin + size;
    for (auto loaded_it = loaded_sections.begin(); loaded_it != loaded_sections.end();) {
        const SectionTableEntry& section = sections_info.code_sections[loaded_it->section_table_index];
        const uint64_t section_begin = static_cast<uint32_t>(loaded_it->loaded_ram_addr);
        const uint64_t section_end = section_begin + section.size;
        if (ranges_overlap(dma_begin, dma_end, section_begin, section_end) &&
            !dma_matches_loaded_section(section, loaded_it->loaded_ram_addr, rom, ram_addr)) {
            trace_rom_dma("INVALIDATE", rom, ram_addr, size, loaded_it->section_table_index,
                callable_map_size());
            loaded_it = erase_loaded_section(loaded_it);
            continue;
        }
        ++loaded_it;
    }

    for (auto pending_it = pending_section_loads.begin(); pending_it != pending_section_loads.end();) {
        const SectionTableEntry& section = sections_info.code_sections[pending_it->section_table_index];
        const uint64_t section_begin = static_cast<uint32_t>(pending_it->loaded_ram_addr);
        const uint64_t section_end = section_begin + section.size;
        if (ranges_overlap(dma_begin, dma_end, section_begin, section_end) &&
            !dma_matches_loaded_section(section, pending_it->loaded_ram_addr, rom, ram_addr)) {
            pending_it = pending_section_loads.erase(pending_it);
            continue;
        }
        ++pending_it;
    }
}

static void add_covered_range(PendingSectionLoad& pending, uint32_t begin, uint32_t end) {
    pending.covered_ranges.push_back({ begin, end });
    std::sort(pending.covered_ranges.begin(), pending.covered_ranges.end(),
        [](const CoveredRange& lhs, const CoveredRange& rhs) { return lhs.begin < rhs.begin; });

    std::vector<CoveredRange> merged;
    merged.reserve(pending.covered_ranges.size());
    for (const CoveredRange& range : pending.covered_ranges) {
        if (merged.empty() || range.begin > merged.back().end) {
            merged.push_back(range);
        }
        else {
            merged.back().end = std::max(merged.back().end, range.end);
        }
    }
    pending.covered_ranges = std::move(merged);
}

static bool section_fully_covered(const PendingSectionLoad& pending, uint32_t section_size) {
    return pending.covered_ranges.size() == 1 && pending.covered_ranges.front().begin == 0 &&
        pending.covered_ranges.front().end >= section_size;
}

void recomp::overlays::register_rom_dma(uint32_t rom, int32_t ram_addr, uint32_t size) {
    if (size == 0 || sections_info.code_sections == nullptr || sections_info.num_code_sections == 0) {
        return;
    }

    invalidate_overwritten_sections(rom, ram_addr, size);

    const uint64_t dma_rom_begin = rom;
    const uint64_t dma_rom_end = dma_rom_begin + size;
    for (size_t section_index = 0; section_index < sections_info.num_code_sections; section_index++) {
        const SectionTableEntry& section = sections_info.code_sections[section_index];
        const uint64_t section_rom_begin = section.rom_addr;
        const uint64_t section_rom_end = section_rom_begin + section.size;
        if (section.size == 0 ||
            !ranges_overlap(dma_rom_begin, dma_rom_end, section_rom_begin, section_rom_end)) {
            continue;
        }

        const int64_t ram_delta = static_cast<int64_t>(section.rom_addr) - static_cast<int64_t>(rom);
        const int64_t section_ram = static_cast<int64_t>(static_cast<uint32_t>(ram_addr)) + ram_delta;
        if (section_ram < 0 || section_ram > UINT32_MAX) {
            continue;
        }
        const int32_t section_ram_addr = static_cast<int32_t>(static_cast<uint32_t>(section_ram));
        const uint32_t covered_begin = static_cast<uint32_t>(
            std::max(dma_rom_begin, section_rom_begin) - section_rom_begin);
        const uint32_t covered_end = static_cast<uint32_t>(
            std::min(dma_rom_end, section_rom_end) - section_rom_begin);

        auto pending_it = std::find_if(pending_section_loads.begin(), pending_section_loads.end(),
            [section_index, section_ram_addr](const PendingSectionLoad& pending) {
                return pending.section_table_index == section_index &&
                    pending.loaded_ram_addr == section_ram_addr;
            });
        if (pending_it == pending_section_loads.end()) {
            pending_it = pending_section_loads.emplace(pending_section_loads.end(),
                PendingSectionLoad{ section_index, section_ram_addr, {} });
        }
        add_covered_range(*pending_it, covered_begin, covered_end);
        trace_rom_dma("MATCH", rom, ram_addr, size, section_index, callable_map_size());

        if (!section_fully_covered(*pending_it, section.size)) {
            continue;
        }

        for (auto loaded_it = loaded_sections.begin(); loaded_it != loaded_sections.end();) {
            if (loaded_it->section_table_index == section_index &&
                loaded_it->loaded_ram_addr == section_ram_addr) {
                loaded_it = erase_loaded_section(loaded_it);
                continue;
            }
            ++loaded_it;
        }
        load_overlay(section_index, section_ram_addr);
        trace_rom_dma("REGISTER", section.rom_addr, section_ram_addr, section.size,
            section_index, callable_map_size());
        pending_section_loads.erase(pending_it);
    }
}

void recomp::overlays::unregister_tlb_mapping(uint32_t virtual_addr, uint32_t size) {
    if (callable_fault_trace_enabled()) {
        std::fprintf(stderr, "RW031_TLB_UNREGISTER_REQUEST virtual=0x%08X size=0x%08X active=%zu\n",
            virtual_addr, size, active_tlb_mappings.size());
        std::fflush(stderr);
    }
    for (size_t mapping_index = 0; mapping_index < active_tlb_mappings.size();) {
        const TlbMapping mapping = active_tlb_mappings[mapping_index];
        if (!mapping_ranges_overlap(
            virtual_addr, size, mapping.virtual_addr, mapping.size)) {
            mapping_index++;
            continue;
        }
        for (size_t binding_index = 0; binding_index < tlb_bound_sections.size();) {
            const TlbBoundSection& binding = tlb_bound_sections[binding_index];
            if (binding.mapping_virtual_addr == mapping.virtual_addr &&
                binding.mapping_physical_addr == mapping.physical_addr &&
                binding.mapping_size == mapping.size) {
                erase_tlb_bound_section(binding_index);
                continue;
            }
            binding_index++;
        }
        active_tlb_mappings.erase(active_tlb_mappings.begin() + mapping_index);
    }
}

void recomp::overlays::register_tlb_mapping(
    uint32_t virtual_addr, uint32_t physical_addr, uint32_t size) {
    if (size == 0 || physical_addr == UINT32_MAX) {
        return;
    }
    unregister_tlb_mapping(virtual_addr, size);
    active_tlb_mappings.push_back(TlbMapping{ virtual_addr, physical_addr, size });
    const TlbMapping& mapping = active_tlb_mappings.back();
    for (const LoadedSection& loaded : loaded_sections) {
        bind_loaded_section_to_mapping(loaded, mapping);
    }
}

void recomp::overlays::unregister_all_tlb_mappings() {
    while (!tlb_bound_sections.empty()) {
        erase_tlb_bound_section(tlb_bound_sections.size() - 1);
    }
    active_tlb_mappings.clear();
}
extern "C" void unload_overlay_by_id(uint32_t id) {
    uint32_t section_table_index = overlays_info.table[id];
    const SectionTableEntry& section = sections_info.code_sections[section_table_index];

    auto find_it = std::find_if(loaded_sections.begin(), loaded_sections.end(), [section_table_index](const LoadedSection& s) { return s.section_table_index == section_table_index; });

    if (find_it != loaded_sections.end()) {
        erase_loaded_section(find_it);
    }
}

extern "C" void load_overlay_by_id(uint32_t id, uint32_t ram_addr) {
    uint32_t section_table_index = overlays_info.table[id];
    const SectionTableEntry& section = sections_info.code_sections[section_table_index];
    int32_t prev_address = section_addresses[section.index];
    if (/*ram_addr >= 0x80000000 && ram_addr < 0x81000000) {*/ prev_address == section.ram_addr) {
        load_overlay(section_table_index, ram_addr);
    }
    else {
        int32_t new_address = prev_address + ram_addr;
        unload_overlay_by_id(id);
        load_overlay(section_table_index, new_address);
    }
}

extern "C" void unload_overlays(int32_t ram_addr, uint32_t size) {
    for (auto it = loaded_sections.begin(); it != loaded_sections.end();) {
        const auto& section = sections_info.code_sections[it->section_table_index];

        // Check if the unloaded region overlaps with the loaded section
        if (ram_addr < (it->loaded_ram_addr + section.size) && (ram_addr + size) >= it->loaded_ram_addr) {
            // Check if the section isn't entirely in the loaded region
            if (ram_addr > it->loaded_ram_addr || (ram_addr + size) < (it->loaded_ram_addr + section.size)) {
                fprintf(stderr,
                    "Cannot partially unload section\n"
                    "  rom: 0x%08X size: 0x%08X loaded_addr: 0x%08X\n"
                    "  unloaded_ram: 0x%08X unloaded_size : 0x%08X\n",
                        section.rom_addr, section.size, it->loaded_ram_addr, ram_addr, size);
                assert(false);
                std::exit(EXIT_FAILURE);
            }
            it = erase_loaded_section(it);
            // Skip incrementing the iterator
            continue;
        }
        ++it;
    }
}

void recomp::overlays::init_overlays() {
    clear_callable_map();
    loaded_sections.clear();
    pending_section_loads.clear();
    active_tlb_mappings.clear();
    tlb_bound_sections.clear();
    section_addresses = (int32_t *)calloc(sections_info.total_num_sections, sizeof(int32_t));

    // Sort the executable sections by rom address
    std::sort(&sections_info.code_sections[0], &sections_info.code_sections[sections_info.num_code_sections],
        [](const SectionTableEntry& a, const SectionTableEntry& b) {
            return a.rom_addr < b.rom_addr;
        }
    );

    for (size_t section_index = 0; section_index < sections_info.num_code_sections; section_index++) {
        SectionTableEntry* code_section = &sections_info.code_sections[section_index];

        section_addresses[sections_info.code_sections[section_index].index] = code_section->ram_addr;
        code_sections_by_rom[code_section->rom_addr] = section_index;        
    }

    load_patch_functions();
}

// Finds a function given a section's index and the function's offset into the section.
bool recomp::overlays::get_func_entry_by_section_index_function_offset(uint16_t code_section_index, uint32_t function_offset, FuncEntry& func_out) {
    if (code_section_index >= sections_info.num_code_sections) {
        return false;
    }

    SectionTableEntry* section = &sections_info.code_sections[code_section_index];
    if (function_offset >= section->size) {
        return false;
    }
    
    // TODO avoid a linear lookup here.
    for (size_t func_index = 0; func_index < section->num_funcs; func_index++) {
        if (section->funcs[func_index].offset == function_offset) {
            func_out = section->funcs[func_index];
            return true;
        }
    }

    return false;
}

void recomp::overlays::register_manual_patch_symbols(const ManualPatchSymbol* manual_patch_symbols) {
    for (size_t i = 0; manual_patch_symbols[i].func != nullptr; i++) {
        if (!manual_patch_symbols_by_vram.emplace(manual_patch_symbols[i].ram_addr, manual_patch_symbols[i].func).second) {
            printf("Duplicate manual patch symbol address: %08X\n", manual_patch_symbols[i].ram_addr);
            ultramodern::error_handling::message_box("Duplicate manual patch symbol address (syms.ld)!");
            assert(false && "Duplicate manual patch symbol address (syms.ld)!");
            ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
        }
    }
}

// TODO use N64Recomp::is_manual_patch_symbol instead after updating submodule.
bool is_manual_patch_symbol(uint32_t vram) {
    return vram >= 0x8F000000 && vram < 0x90000000;
}

// Finds a function given a section's index and the function's offset into the section and returns its native pointer.
recomp_func_t* recomp::overlays::get_func_by_section_index_function_offset(uint16_t code_section_index, uint32_t function_offset) {
    FuncEntry entry;
    
    if (get_func_entry_by_section_index_function_offset(code_section_index, function_offset, entry)) {
        return entry.func;
    }

    if (code_section_index == N64Recomp::SectionAbsolute && is_manual_patch_symbol(function_offset)) {
        auto find_it = manual_patch_symbols_by_vram.find(function_offset);
        if (find_it != manual_patch_symbols_by_vram.end()) {
            return find_it->second;
        }
    }

    return nullptr;
}

// Finds a function given a section's rom address and the function's vram address.
recomp_func_t* recomp::overlays::get_func_by_section_rom_function_vram(uint32_t section_rom, uint32_t function_vram) {
    auto find_section_it = code_sections_by_rom.find(section_rom);
    if (find_section_it == code_sections_by_rom.end()) {
        return nullptr;
    }

    SectionTableEntry* section = &sections_info.code_sections[find_section_it->second];
    int32_t func_offset = function_vram - section->ram_addr;
    
    return get_func_by_section_index_function_offset(find_section_it->second, func_offset);
}

extern "C" recomp_func_t * get_function(int32_t addr) {
    recomp_func_t *function = recomp::overlays::find_resident_callable(addr);
    if (function == nullptr) {
        if (callable_failure_handler) callable_failure_handler(nullptr, addr);
        fprintf(stderr, "Failed to find function at 0x%08X\n", addr);
        assert(false);
        std::exit(EXIT_FAILURE);
    }
    return function;
}

recomp_func_t* recomp::overlays::find_resident_callable(int32_t addr) {
    std::lock_guard lock(callable_map_mutex);
    const auto func_find = func_map.find(addr);
    return func_find != func_map.end() ? func_find->second : nullptr;
}


static recomp_func_t* find_callable_with_identity(int32_t addr,
    recomp::overlays::CallableMappingIdentity& identity) {
    identity = {};
    std::lock_guard lock(callable_map_mutex);
    const auto active = func_map.find(addr);
    if (active == func_map.end()) return nullptr;
    const auto record = callable_identities.find(addr);
    if (record != callable_identities.end()) identity = record->second;
    identity.active_callable = true;
    return active->second;
}

bool recomp::overlays::get_active_callable_identity(
    int32_t addr, recomp_func_t* function, CallableMappingIdentity& identity) {
    identity = {};
    std::lock_guard lock(callable_map_mutex);
    const auto active = func_map.find(addr);
    if (active == func_map.end() || active->second != function) return false;
    const auto record = callable_identities.find(addr);
    if (record != callable_identities.end()) identity = record->second;
    identity.active_callable = true;
    return identity.match_count != 0;
}

recomp_func_t* recomp::overlays::get_generated_callable(int32_t addr) {
    const uint32_t target = static_cast<uint32_t>(addr);
    for (size_t section_index = 0; section_index < sections_info.num_code_sections; section_index++) {
        const SectionTableEntry& section = sections_info.code_sections[section_index];
        for (size_t function_index = 0; function_index < section.num_funcs; function_index++) {
            if (section.ram_addr + section.funcs[function_index].offset == target) {
                return section.funcs[function_index].func;
            }
        }
    }
    return nullptr;
}

bool recomp::overlays::is_generated_callable(int32_t addr) {
    return get_generated_callable(addr) != nullptr;
}

recomp_func_t* recomp::overlays::resolve_callable_with_identity(
    uint8_t* rdram, recomp_context* ctx, int32_t addr, CallableMappingIdentity& identity) {
    if (recomp_func_t *function = find_callable_with_identity(addr, identity)) {
        return function;
    }

    trace_callable_fault("MISS", addr, false, false, nullptr);
    if (callable_resolver != nullptr) {
        if (recomp_func_t *function = callable_resolver(rdram, ctx, addr)) {
            trace_callable_fault("RESOLVED", addr, false, true, function);
            // A resolver may return a validated declared/native pointer without
            // publishing a loaded mapping. Leave that ROM identity unavailable.
            get_active_callable_identity(addr, function, identity);
            return function;
        }
    }

    trace_callable_fault("FAILED", addr, false, false, nullptr);
    if (callable_failure_handler) callable_failure_handler(ctx, addr);
    fprintf(stderr, "Failed to find function at 0x%08X\n", addr);
    assert(false);
    std::exit(EXIT_FAILURE);
}
extern "C" recomp_func_t* get_function_with_context(
    uint8_t* rdram, recomp_context* ctx, int32_t addr) {
    recomp::overlays::CallableMappingIdentity identity;
    return recomp::overlays::resolve_callable_with_identity(rdram, ctx, addr, identity);
}

std::unordered_map<recomp_func_t*, recomp::overlays::BasePatchedFunction> recomp::overlays::get_base_patched_funcs() {
    std::unordered_map<recomp_func_t*, BasePatchedFunction> ret{};

    // Collect the set of all functions in the patches.
    std::unordered_map<recomp_func_t*, BasePatchedFunction> all_patch_funcs{};
    for (size_t patch_section_index = 0; patch_section_index < num_patch_code_sections; patch_section_index++) {
        const auto& patch_section = patch_code_sections[patch_section_index];
        for (size_t func_index = 0; func_index < patch_section.num_funcs; func_index++) {
            all_patch_funcs.emplace(patch_section.funcs[func_index].func, BasePatchedFunction{ .patch_section = patch_section_index, .function_index = func_index });
        }
    }

    // Check every vanilla function against the full patch function set.
    // Any functions in both are patched.
    for (size_t code_section_index = 0; code_section_index < sections_info.num_code_sections; code_section_index++) {
        const auto& code_section = sections_info.code_sections[code_section_index];
        for (size_t func_index = 0; func_index < code_section.num_funcs; func_index++) {
            recomp_func_t* cur_func = code_section.funcs[func_index].func;
            // If this function also exists in the patches function set then it's a vanilla function that was patched.
            auto find_it = all_patch_funcs.find(cur_func);
            if (find_it != all_patch_funcs.end()) {
                ret.emplace(cur_func, find_it->second);
            }
        }
    }

    return ret;
}

const std::unordered_map<uint32_t, uint16_t>& recomp::overlays::get_patch_vrom_to_section_map() {
    return patch_code_sections_by_rom;
}

uint32_t recomp::overlays::get_patch_section_ram_addr(uint16_t patch_code_section_index) {
    if (patch_code_section_index < num_patch_code_sections) {
        return patch_code_sections[patch_code_section_index].ram_addr;
    }
    assert(false);
    return -1;
}

uint32_t recomp::overlays::get_patch_section_rom_addr(uint16_t patch_code_section_index) {
    if (patch_code_section_index < num_patch_code_sections) {
        return patch_code_sections[patch_code_section_index].rom_addr;
    }
    assert(false);
    return -1;
}

const FuncEntry* recomp::overlays::get_patch_function_entry(uint16_t patch_code_section_index, size_t function_index) {
    if (patch_code_section_index < num_patch_code_sections) {
        const auto& section = patch_code_sections[patch_code_section_index];
        if (function_index < section.num_funcs) {
            return &section.funcs[function_index];
        }
    }
    assert(false);
    return nullptr;
}

// Finds a base patched function given a patch section's index and the function's offset into the section.
bool recomp::overlays::get_patch_func_entry_by_section_index_function_offset(uint16_t patch_code_section_index, uint32_t function_offset, FuncEntry& func_out) {
    if (patch_code_section_index >= num_patch_code_sections) {
        return false;
    }

    SectionTableEntry* section = &patch_code_sections[patch_code_section_index];
    if (function_offset >= section->size) {
        return false;
    }
    
    // TODO avoid a linear lookup here.
    for (size_t func_index = 0; func_index < section->num_funcs; func_index++) {
        if (section->funcs[func_index].offset == function_offset) {
            func_out = section->funcs[func_index];
            return true;
        }
    }

    return false;
}

std::span<const RelocEntry> recomp::overlays::get_patch_section_relocs(uint16_t patch_code_section_index) {
    if (patch_code_section_index < num_patch_code_sections) {
        const auto& section = patch_code_sections[patch_code_section_index];
        return std::span{ section.relocs, section.num_relocs };
    }
    assert(false);
    return {};
}

std::span<const uint8_t> recomp::overlays::get_patch_binary() {
    return std::span{ reinterpret_cast<const uint8_t*>(patch_data.data()), patch_data.size() };
}
