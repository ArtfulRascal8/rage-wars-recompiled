#ifndef __RECOMP_OVERLAYS_H__
#define __RECOMP_OVERLAYS_H__

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <span>
#include "sections.h"

namespace recomp {
    // Host-controlled handling for generated-code break sentinels. The
    // default runtime behavior remains fatal when no handler is installed.
    using break_handler_t = void (*)(uint32_t vram);
    void set_break_handler(break_handler_t handler);

    namespace overlays {
        struct overlay_section_table_data_t {
            SectionTableEntry* code_sections;
            size_t num_code_sections;
            size_t total_num_sections;
        };

        struct overlays_by_index_t {
            int* table;
            size_t len;
        };

        void register_overlays(const overlay_section_table_data_t& sections, const overlays_by_index_t& overlays);

        void register_patches(const char* patch_data, size_t patch_size, SectionTableEntry* code_sections, size_t num_sections);
        void register_base_export(const std::string& name, recomp_func_t* func);
        void register_ext_base_export(const std::string& name, recomp_func_ext_t* func);
        void register_base_exports(const FunctionExport* exports);
        void register_base_events(char const* const* event_names);
        void register_manual_patch_symbols(const ManualPatchSymbol* manual_patch_symbols);
        void read_patch_data(uint8_t* rdram, gpr patch_data_address);

        void init_overlays();
        const std::unordered_map<uint32_t, uint16_t>& get_vrom_to_section_map();
        uint32_t get_section_ram_addr(uint16_t code_section_index);
        std::span<const RelocEntry> get_section_relocs(uint16_t code_section_index);
        recomp_func_t* get_func_by_section_rom_function_vram(uint32_t section_rom, uint32_t function_vram);
        bool get_func_entry_by_section_index_function_offset(uint16_t code_section_index, uint32_t function_offset, FuncEntry& func_out);
        recomp_func_t* get_func_by_section_index_function_offset(uint16_t code_section_index, uint32_t function_offset);
        recomp_func_t* get_base_export(const std::string& export_name);
        recomp_func_ext_t* get_ext_base_export(const std::string& export_name);
        size_t get_base_event_index(const std::string& event_name);
        size_t num_base_events();

        void add_loaded_function(int32_t ram_addr, recomp_func_t* func);

        // Neutral adapter surface. Game-owned code-residency services decide
        // whether and how a missing callable should become resident.
        using callable_resolver_t = recomp_func_t* (*)(
            uint8_t* rdram, recomp_context* ctx, int32_t vram);
        void set_callable_resolver(callable_resolver_t resolver);
        // Notification only: unresolved calls remain fatal after the handler returns.
        using callable_failure_handler_t = void (*)(recomp_context* ctx, int32_t vram);
        void set_callable_failure_handler(callable_failure_handler_t handler);
        recomp_func_t* find_resident_callable(int32_t vram);

        // Read-only provenance for the callable currently selected in func_map.
        // A successful native/manual mapping may have no generated ROM identity.
        struct CallableMappingIdentity {
            bool active_callable = false;
            bool tlb_bound = false;
            uint32_t match_count = 0;
            uint32_t section_rom = UINT32_MAX;
            uint32_t function_rom = UINT32_MAX;
            uint32_t original_function_vram = UINT32_MAX;
            uint32_t loaded_section_vram = UINT32_MAX;
            uint32_t physical_section_addr = UINT32_MAX;
            uint32_t function_offset = UINT32_MAX;
            uint32_t function_size = 0;
            size_t logical_section_index = SIZE_MAX;
            size_t code_section_index = SIZE_MAX;
        };
        bool get_active_callable_identity(int32_t vram, recomp_func_t* function,
                                         CallableMappingIdentity& identity);

        // Same resolution path as get_function_with_context, with the pointer
        // and loaded-ROM record copied atomically at the winning lookup.
        recomp_func_t* resolve_callable_with_identity(uint8_t* rdram, recomp_context* ctx,
            int32_t vram, CallableMappingIdentity& identity);

        recomp_func_t* get_generated_callable(int32_t vram);
        bool is_generated_callable(int32_t vram);

        // Bind/unbind ROM-backed executable sections to the guest virtual
        // ranges made present by the runtime TLB.
        void register_tlb_mapping(uint32_t virtual_addr, uint32_t physical_addr, uint32_t size);
        void unregister_tlb_mapping(uint32_t virtual_addr, uint32_t size);
        void unregister_all_tlb_mappings();

        // Register code made resident by a cart-ROM DMA. Unlike load_overlays(),
        // this path tracks partial DMA coverage and only exposes a section after
        // every byte of that section has reached its destination.
        void register_rom_dma(uint32_t rom, int32_t ram_addr, uint32_t size);

        struct BasePatchedFunction {
            size_t patch_section;
            size_t function_index;
        };

        std::unordered_map<recomp_func_t*, BasePatchedFunction> get_base_patched_funcs();
        const std::unordered_map<uint32_t, uint16_t>& get_patch_vrom_to_section_map();
        uint32_t get_patch_section_ram_addr(uint16_t patch_code_section_index);
        uint32_t get_patch_section_rom_addr(uint16_t patch_code_section_index);
        const FuncEntry* get_patch_function_entry(uint16_t patch_code_section_index, size_t function_index);
        bool get_patch_func_entry_by_section_index_function_offset(uint16_t code_section_index, uint32_t function_offset, FuncEntry& func_out);
        std::span<const RelocEntry> get_patch_section_relocs(uint16_t patch_code_section_index);
        std::span<const uint8_t> get_patch_binary();
    }
};

extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" void unload_overlays(int32_t ram_addr, uint32_t size);

#endif
