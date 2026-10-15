#include "rage_wars_native_pc_options.h"
#include "recomp_memory_shim.h"

void trace_func_002A1524_r000A2124(uint8_t* rdram, recomp_context* ctx);

/* Native focus accessor 00299948 reads manager -> stack[index].focus. Prefix
 * text under that exact focus owner while paused. Root pause (page 1) uses
 * unwrapped type-9 rows; Options uses B61 wrappers. The native glyph builder
 * consumes the bytes immediately (0040D9F0) into frame-owned glyph records.
 * Use its frame allocator, never a host pointer or persistent guest scratch. */
static int focus_span(uint32_t address, uint32_t size) {
    return address >= 0x80000000U && size <= 0x00800000U &&
        address - 0x80000000U <= 0x00800000U - size;
}
/* The original text batch stops advancing when a queued glyph has no font
 * resource (00410FD4 returns zero for zero-sized glyphs). Check each possible
 * template font, including FF inheritance, before adding a marker. Results
 * font 2 has no '>' glyph, but does contain '*'. Keep the label unchanged if
 * neither marker is available. */
static int focus_glyph_supported(uint8_t* rdram, uint32_t node, uint32_t glyph) {
    const uint32_t fonts = (uint32_t)MEM_W(0x8014B998, 0);
    const int normal = MEM_B(0x2C, node);
    unsigned pair;
    if (!focus_span(fonts, 0x49C) || (fonts & 3)) return 0;
    for (pair = 0; pair < 3; ++pair) {
        int font = MEM_B(0x2C + pair * 2, node);
        uint32_t record;
        if (font == -1) font = normal;
        if (font < 0) return 0;
        record = fonts + (uint32_t)font * 0x49C;
        if (!focus_span(record, 0x49C)) return 0;
        if (MEM_B(8 + glyph * 8, record) <= 0 ||
            MEM_B(9 + glyph * 8, record) <= 0) return 0;
    }
    return 1;
}
static uint32_t focus_marker(uint8_t* rdram, uint32_t node) {
    if (focus_glyph_supported(rdram, node, '>' - 0x20)) return '>';
    if (focus_glyph_supported(rdram, node, '*' - 0x20)) return '*';
    return 0;
}
uint32_t xr64_pause_focus_text(uint8_t* rdram, recomp_context* ctx,
                              uint32_t node, uint32_t text) {
    uint32_t manager, stack, index, focus, ancestor, size, destination;
    recomp_context allocation;
    uint32_t marker;
    if (MEM_W(0x801407D4, 0) == 0 || !focus_span(node, 0x3C) ||
        !focus_span(text, 1)) return text;
    manager = (uint32_t)MEM_W(0x80144E00, 0);
    if (!focus_span(manager, 0x10) || (manager & 3)) return text;
    index = (uint32_t)MEM_W(4, manager);
    stack = (uint32_t)MEM_W(0xC, manager);
    if (index >= 32 || (stack & 3) || !focus_span(stack, (index + 1) * 0x1C)) return text;
    focus = (uint32_t)MEM_W(index * 0x1C + 8, stack);
    if ((focus & 3) || !focus_span(focus, 0x14) ||
        (MEM_HU(0x12, focus) & 0x100)) return text;
    if (MEM_HU(0xE, focus) != 0xB61 &&
        !(MEM_HU(0xE, focus) == 9 && MEM_W(index * 0x1C + 4, stack) == 1)) return text;
    ancestor = node;
    for (size = 0; size < 32 && ancestor != focus; ++size) {
        if ((ancestor & 3) || !focus_span(ancestor, 4)) return text;
        ancestor = (uint32_t)MEM_W(0, ancestor);
    }
    if (ancestor != focus) return text;
    marker = focus_marker(rdram, node);
    if (marker == 0) return text;
    for (size = 0; size < 125; ++size) {
        if (!focus_span(text + size, 1)) return text;
        if (MEM_BU(size, text) == 0) break;
    }
    if (size == 0 || size == 125) return text;
    allocation = *ctx;
    allocation.f_odd = allocation.mips3_float_mode ?
        &allocation.f1.u32l : &allocation.f0.u32h;
    allocation.r4 = size + 3;
    trace_func_002A1524_r000A2124(rdram, &allocation);
    destination = (uint32_t)allocation.r2;
    if (!focus_span(destination, size + 3)) return text;
    MEM_B(0, destination) = marker;
    MEM_B(1, destination) = ' ';
    for (index = 0; index <= size; ++index) MEM_B(index + 2, destination) = MEM_BU(index, text);
    return destination;
}
