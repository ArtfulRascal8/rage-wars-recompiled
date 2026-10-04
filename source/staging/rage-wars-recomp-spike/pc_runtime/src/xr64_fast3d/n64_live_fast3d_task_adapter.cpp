#include "n64_live_fast3d_task_adapter.hpp"

#include "fast3d_scene_decoder.hpp"
#include "n64_segment_map.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace xr64 {
namespace {

constexpr size_t maximum_rdram_bytes = 8U * 1024U * 1024U;
constexpr size_t minimum_rdram_bytes = 4U * 1024U * 1024U;
constexpr size_t default_segment_copy_bytes = 64U * 1024U;
constexpr size_t maximum_segment_copy_bytes = 4U * 1024U * 1024U;
constexpr size_t maximum_total_segment_bytes = 32U * 1024U * 1024U;
constexpr size_t maximum_commands = 100000U;
constexpr size_t maximum_depth = 32U;
constexpr uint8_t command_display_list = 0x06U;
constexpr uint8_t command_move_word = 0xbcU;
constexpr uint8_t command_end_display_list = 0xb8U;
constexpr uint8_t f3dex2_command_display_list = 0xdeU;
constexpr uint8_t f3dex2_command_move_word = 0xdbU;
constexpr uint8_t f3dex2_command_end_display_list = 0xdfU;
constexpr uint8_t move_word_segment = 0x06U;
constexpr uint32_t kseg0_base = 0x80000000U;
constexpr uint32_t kseg1_base = 0xa0000000U;
constexpr uint32_t physical_address_mask = 0x1fffffffU;

uint32_t read_be32(const std::vector<uint8_t> &bytes, size_t offset) {
    return (static_cast<uint32_t>(bytes[offset]) << 24U) |
            (static_cast<uint32_t>(bytes[offset + 1U]) << 16U) |
            (static_cast<uint32_t>(bytes[offset + 2U]) << 8U) |
            static_cast<uint32_t>(bytes[offset + 3U]);
}

class SegmentScanner {
public:
    SegmentScanner(
            const std::vector<uint8_t> &rdram_value,
            const N64LiveFast3DSegmentState &initial_state,
            N64MicrocodeFamily family_value) :
            rdram(rdram_value),
            bases(initial_state.bases),
            loaded(initial_state.loaded),
            family(family_value) {
        bases[0] = 0U;
        loaded[0] = true;
    }

    bool scan(uint32_t root_address, std::string &error) {
        return scan_display_list(root_address, 0U, error);
    }

    const std::array<uint32_t, 32> &get_bases() const {
        return bases;
    }

    const std::array<bool, 32> &get_loaded() const {
        return loaded;
    }

    const std::array<size_t, 32> &get_required_bytes() const {
        return required_bytes;
    }

private:
    const std::vector<uint8_t> &rdram;
    std::array<uint32_t, 32> bases{};
    std::array<bool, 32> loaded{};
    std::array<size_t, 32> required_bytes{};
    size_t command_count = 0;
    N64MicrocodeFamily family = N64MicrocodeFamily::Unknown;

    uint8_t display_list_opcode() const {
        return family == N64MicrocodeFamily::F3DEX2 ?
                f3dex2_command_display_list : command_display_list;
    }

    uint8_t move_word_opcode() const {
        return family == N64MicrocodeFamily::F3DEX2 ?
                f3dex2_command_move_word : command_move_word;
    }

    uint8_t end_display_list_opcode() const {
        return family == N64MicrocodeFamily::F3DEX2 ?
                f3dex2_command_end_display_list : command_end_display_list;
    }

    static bool is_direct_rdram_pointer(uint32_t address) {
        const uint32_t region = address & 0xe0000000U;
        return region == kseg0_base || region == kseg1_base;
    }

    static uint8_t logical_segment(uint32_t address) {
        return is_direct_rdram_pointer(address) ? 0U :
                static_cast<uint8_t>(address >> 24U);
    }

    bool resolve(uint32_t address, size_t &physical, std::string &error) {
        uint64_t result = 0;
        if (is_direct_rdram_pointer(address)) {
            result = address & physical_address_mask;
        } else {
            const uint8_t segment = static_cast<uint8_t>(address >> 24U);
            const uint32_t offset = address & 0x00ffffffU;
            if (segment >= loaded.size() || !loaded[segment]) {
                error = "Live GBI referenced unset segment " +
                        std::to_string(segment) + " at address " +
                        std::to_string(address) + ".";
                return false;
            }
            result = static_cast<uint64_t>(bases[segment]) + offset;
        }
        if (result > rdram.size() || 8U > rdram.size() - result) {
            error = "Live GBI display-list address is outside RDRAM.";
            return false;
        }
        physical = static_cast<size_t>(result);
        return true;
    }

    bool scan_display_list(
            uint32_t address, size_t depth, std::string &error) {
        if (depth > maximum_depth) {
            error = "Live GBI exceeded the display-list recursion limit.";
            return false;
        }
        size_t cursor = 0;
        if (!resolve(address, cursor, error)) return false;
        const uint8_t segment = logical_segment(address);
        const size_t segment_base = segment == 0U ? 0U : bases[segment];

        while (cursor <= rdram.size() - 8U) {
            if (++command_count > maximum_commands) {
                error = "Live GBI exceeded the command scan limit.";
                return false;
            }
            const uint32_t word0 = read_be32(rdram, cursor);
            const uint32_t word1 = read_be32(rdram, cursor + 4U);
            cursor += 8U;
            required_bytes[segment] = std::max(
                    required_bytes[segment], cursor - segment_base);
            const uint8_t opcode = static_cast<uint8_t>(word0 >> 24U);
            if (opcode == move_word_opcode() &&
                    ((word0 & 0xffU) == move_word_segment ||
                     static_cast<uint8_t>(word0 >> 16U) == move_word_segment)) {
                const uint32_t segment_index =
                        (word0 & 0xffU) == move_word_segment ?
                                ((word0 >> 8U) & 0xffffU) >> 2U :
                                (word0 & 0xffffU) >> 2U;
                const uint32_t base = word1 & 0x00ffffffU;
                if (segment_index >= loaded.size() || base >= rdram.size()) {
                    error = "Live GBI set an invalid segment base.";
                    return false;
                }
                bases[segment_index] = base;
                loaded[segment_index] = true;
                continue;
            }
            if (opcode == display_list_opcode()) {
                if (!scan_display_list(word1, depth + 1U, error)) return false;
                if (((word0 >> 16U) & 0xffU) != 0U) return true;
                continue;
            }
            if (opcode == end_display_list_opcode()) return true;
        }
        error = "Live GBI display list did not terminate inside RDRAM.";
        return false;
    }
};

bool recover_segment_state_from_dmem(
        const N64MemoryReader &memory,
        const std::vector<uint8_t> &rdram,
        uint32_t root_address,
        N64MicrocodeFamily family,
        const N64LiveFast3DSegmentState &initial_state,
        N64LiveFast3DSegmentState &recovered_state,
        std::string &error) {
    std::vector<uint8_t> dmem;
    if (!memory.read(N64MemorySpace::Dmem, 0U, 4096U, dmem, error) ||
            dmem.size() != 4096U) {
        return false;
    }

    bool found = false;
    N64LiveFast3DSegmentState unique_state;
    for (size_t offset = 0; offset + 16U * 4U <= dmem.size();
            offset += 4U) {
        if (read_be32(dmem, offset) != 0U) continue;

        N64LiveFast3DSegmentState candidate = initial_state;
        candidate.bases[0] = 0U;
        candidate.loaded[0] = true;
        size_t nonzero_bases = 0U;
        for (size_t segment = 1; segment < 16U; ++segment) {
            const uint32_t base = read_be32(dmem, offset + segment * 4U) &
                    0x00ffffffU;
            if (base == 0U || base >= rdram.size() || (base & 7U) != 0U) {
                continue;
            }
            candidate.bases[segment] = base;
            candidate.loaded[segment] = true;
            ++nonzero_bases;
        }
        if (nonzero_bases < 2U) continue;

        SegmentScanner probe(rdram, candidate, family);
        std::string probe_error;
        if (!probe.scan(root_address, probe_error)) continue;
        candidate.bases = probe.get_bases();
        candidate.loaded = probe.get_loaded();

        if (!found) {
            unique_state = candidate;
            found = true;
        } else if (unique_state.bases != candidate.bases ||
                unique_state.loaded != candidate.loaded) {
            error = "Live GBI found multiple valid RSP segment tables.";
            return false;
        }
    }

    if (!found) {
        error = "Live GBI could not recover a valid segment table from RSP DMEM.";
        return false;
    }
    recovered_state = unique_state;
    error.clear();
    return true;
}

bool read_rdram_snapshot(
        const N64MemoryReader &memory,
        std::vector<uint8_t> &rdram,
        std::string &error) {
    if (memory.read(N64MemorySpace::Rdram, 0U,
                maximum_rdram_bytes, rdram, error)) {
        return rdram.size() == maximum_rdram_bytes;
    }
    error.clear();
    if (!memory.read(N64MemorySpace::Rdram, 0U,
                minimum_rdram_bytes, rdram, error) ||
            rdram.size() != minimum_rdram_bytes) {
        if (error.empty()) error = "Could not snapshot bounded N64 RDRAM.";
        return false;
    }
    return true;
}

} // namespace

bool translate_n64_live_fast3d_task(
        const N64MemoryReader &memory,
        const N64GraphicsTaskDescriptor &task,
        N64LiveFast3DSegmentState &segment_state,
        RenderSceneData &scene,
        std::string &error) {
    scene = {};
    const bool compatible_gbi_family =
            task.microcode_family == N64MicrocodeFamily::Fast3D ||
            task.microcode_family == N64MicrocodeFamily::F3DEX ||
            task.microcode_family == N64MicrocodeFamily::F3DEX2;
    std::vector<uint8_t> rdram;
    N64SegmentMap segments;
    if (!compatible_gbi_family || task.task_data_size == 0U) {
        error = "Live task is not an explicitly supported Fast3D, F3DEX, or F3DEX2 task.";
        return false;
    }

    if (!read_rdram_snapshot(memory, rdram, error)) return false;
    if (task.task_data_address >= rdram.size() ||
            static_cast<uint64_t>(task.task_data_address) +
                    task.task_data_size > rdram.size()) {
        error = "Live GBI task data is outside the RDRAM snapshot.";
        return false;
    }

    N64LiveFast3DSegmentState decoder_initial_segment_state = segment_state;
    SegmentScanner scanner(rdram, decoder_initial_segment_state,
            task.microcode_family);
    bool scan_ok = scanner.scan(task.task_data_address, error);
    std::array<uint32_t, 32> resolved_bases = scanner.get_bases();
    std::array<bool, 32> resolved_loaded = scanner.get_loaded();
    std::array<size_t, 32> resolved_required = scanner.get_required_bytes();
    if (!scan_ok && error.find("referenced unset segment") != std::string::npos) {
        N64LiveFast3DSegmentState recovered;
        if (recover_segment_state_from_dmem(
                memory, rdram, task.task_data_address, task.microcode_family,
                segment_state, recovered, error)) {
            SegmentScanner recovered_scanner(rdram, recovered,
                    task.microcode_family);
            decoder_initial_segment_state = recovered;
            scan_ok = recovered_scanner.scan(task.task_data_address, error);
            resolved_bases = recovered_scanner.get_bases();
            resolved_loaded = recovered_scanner.get_loaded();
            resolved_required = recovered_scanner.get_required_bytes();
        }
    }
    segment_state.bases = resolved_bases;
    segment_state.loaded = resolved_loaded;
    if (!scan_ok) return false;

    segments.live_rdram = std::move(rdram);
    segments.live_segment_bases = decoder_initial_segment_state.bases;
    segments.live_segment_loaded = decoder_initial_segment_state.loaded;
    segments.live_segment_bases[0] = 0U;
    segments.live_segment_loaded[0] = true;
    size_t total_bytes = 0U;
    const auto &bases = resolved_bases;
    const auto &loaded = resolved_loaded;
    for (size_t segment = 0; segment < loaded.size(); ++segment) {
        if (!loaded[segment]) continue;
        const size_t base = segment == 0U ? 0U : bases[segment];
        const size_t remaining = segments.live_rdram.size() - base;
        size_t count = std::min(remaining, default_segment_copy_bytes);
        if (segment == 0U) {
            const uint64_t root_end =
                    static_cast<uint64_t>(task.task_data_address) +
                    task.task_data_size;
            if (root_end <= remaining) {
                count = std::max(count, static_cast<size_t>(root_end));
            }
        }
        // Segment bases can overlap or point inside another segment; an
        // adjacent base is not a valid upper bound on referenced texture data.
        count = std::max(count, resolved_required[segment]);
        count = std::min(count, maximum_segment_copy_bytes);
        count = std::min(count, remaining);
        if (count == 0U || count > maximum_total_segment_bytes - total_bytes) {
            error = "Live GBI segment snapshot exceeded its memory budget.";
            return false;
        }
        segments.bytes[segment].assign(
                segments.live_rdram.begin() + static_cast<std::ptrdiff_t>(base),
                segments.live_rdram.begin() + static_cast<std::ptrdiff_t>(base + count));
        segments.loaded[segment] = true;
        total_bytes += count;
    }

    const DisplayListRoot root{task.task_data_address, 0U, false, false};
    if (!decode_n64_gbi_scene(
                task.microcode_family, segments, {root}, scene, error)) {
        return false;
    }
    scene.fast3d.provenance =
            Fast3DStateProvenance::LiveRdramTaskSnapshot;
    scene.fast3d.segment_confidence = Fast3DStateConfidence::Captured;
    scene.fast3d.segment_state_available = true;
    scene.fast3d.segment_bases = resolved_bases;
    for (size_t segment = 0; segment < resolved_loaded.size(); ++segment) {
        scene.fast3d.segment_loaded[segment] = resolved_loaded[segment] ? 1U : 0U;
    }
    return true;
}
} // namespace xr64
