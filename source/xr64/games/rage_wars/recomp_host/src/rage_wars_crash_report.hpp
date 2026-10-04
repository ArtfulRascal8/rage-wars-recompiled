#pragma once
#include <cstdint>
#include <filesystem>
#include "recomp.h"
namespace xr64::rage_wars::crash {
void initialize(const std::filesystem::path& directory, bool show_window);
void note_lookup(std::int32_t target, const char* file, int line) noexcept;

struct CallIdentity {
    bool active_callable = false;
    bool tlb_bound = false;
    std::uint32_t match_count = 0;
    std::uint32_t section_rom = UINT32_MAX;
    std::uint32_t function_rom = UINT32_MAX;
    std::uint32_t original_function_vram = UINT32_MAX;
    std::uint32_t loaded_section_vram = UINT32_MAX;
    std::uint32_t physical_section_addr = UINT32_MAX;
    std::uint32_t logical_section_index = UINT32_MAX;
    std::uint32_t code_section_index = UINT32_MAX;
    std::uintptr_t native_function = 0;
};
std::uint64_t begin_indirect_call(std::int32_t target, std::uint32_t caller_pc,
    std::uint32_t caller_rom, std::uint32_t caller_function_rom, std::uint32_t caller_section_rom,
    std::uint32_t caller_section,
    const char* file, int line, const recomp_context* context) noexcept;
void resolved_indirect_call(std::uint64_t token, const CallIdentity& identity) noexcept;
void complete_indirect_call(std::uint64_t token, bool unwound) noexcept;

void missing_callable(recomp_context* context, std::int32_t target);
}
