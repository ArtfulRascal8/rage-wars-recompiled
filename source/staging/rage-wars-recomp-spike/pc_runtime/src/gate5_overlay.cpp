#include "gate5_runtime.hpp"

#include <librecomp/overlays.hpp>

#include "recomp_overlays.inl"

void rage_wars_register_generated_overlays() {
    recomp::overlays::register_overlays(
            {section_table, ARRLEN(section_table), num_sections},
            {overlay_sections_by_index, ARRLEN(overlay_sections_by_index)});
}

std::size_t rage_wars_generated_section_count() {
    return ARRLEN(section_table);
}
