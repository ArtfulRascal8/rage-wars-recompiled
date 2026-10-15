#include "rage_wars_native_pc_options.h"

#include "funcs.h"
#include <stdio.h>

/*
 * PC OPTIONS and VR OPTIONS are two native B61 gateway buttons added to the
 * Options page (pause resource 0x16, title/lobby resource 4). Their pages reuse
 * the page's native buttons, both gateways and off-screen extra wrappers, so a
 * page shows up to XR64_PC_MAX_ROWS rows; longer lists page with MORE. Native
 * value widgets are hidden and restored, never reinterpreted as buttons. All
 * injected sources and labels share one owned guest allocation, detached and
 * freed before the page resource is released; native E03 owns the wrappers.
 */
enum {
    XR64_PC_SOURCE_STRIDE = 0xC0,
    XR64_PC_OPTIONS_TEXT_OFFSET = 0x44,
    XR64_PC_OPTIONS_TAIL_OFFSET = 0x80,
    XR64_PC_OPTIONS_LABEL_CAPACITY = 0x20,
    XR64_PC_GATEWAYS = 2,
    XR64_PC_MAX_ROWS = 10,
    XR64_PC_MAX_INJECTED = XR64_PC_GATEWAYS + XR64_PC_MAX_ROWS,
    XR64_PC_MAX_NODES = 10 + XR64_PC_MAX_INJECTED,
    XR64_PC_ROW_HEIGHT = 12,
};

static uint32_t s_pc_options_block;
static uint32_t s_pc_options_block_size;
static uint32_t s_pc_options_wrapper;
static uint32_t s_page16_root;
static uint32_t s_row_parent;
static int16_t s_parent_height;
static int s_pc_options_active;
static unsigned s_pc_page;
enum { PC_HOME, PC_DISPLAY, PC_CONTROLS, PC_CONTROLLER, PC_MOUSE, PC_BINDINGS, PC_BIND_DETAIL,
       PC_VR, PC_VR_MOVE, PC_VR_VIEW, PC_PLAYERS, PC_PAGE_COUNT };
static unsigned s_binding_device, s_binding_parent, s_page_offset, s_binding_offset;
static uint64_t s_ui_revision;

/* Page contents; BACK is implicit and MORE is added when a list overflows. */
static const uint32_t s_pc_home[] = {XR64_PC_OPEN_DISPLAY, XR64_PC_OPEN_CONTROLS, XR64_PC_OPEN_CONTROLLER,
    XR64_PC_OPEN_MOUSE, XR64_PC_OPEN_KEYBOARD, XR64_PC_OPEN_PLAYERS, XR64_PC_WEAPON_MODELS};
static const uint32_t s_pc_display[] = {XR64_PC_OPTION_RESOLUTION, XR64_PC_OPTION_DISPLAY_MODE,
    XR64_PC_OPTION_ASPECT_RATIO, XR64_PC_OPTION_FOV, XR64_PC_OPTION_MASTER_VOLUME};
static const uint32_t s_pc_controls[] = {XR64_PC_INPUT_PROFILE, XR64_PC_INPUT_DEVICE, XR64_PC_LOOK_SPRING,
    XR64_PC_INPUT_CURVE, XR64_PC_AIM_DEADZONE, XR64_PC_MOVE_THRESHOLD, XR64_PC_SWAP_STICKS};
static const uint32_t s_pc_controller[] = {XR64_PC_PAD_SENS_X, XR64_PC_PAD_SENS_Y, XR64_PC_PAD_INVERT_X,
    XR64_PC_PAD_INVERT_Y, XR64_PC_OPEN_PAD_BINDINGS, XR64_PC_PAD_RESET};
static const uint32_t s_pc_mouse[] = {XR64_PC_MOUSE_SENS_X, XR64_PC_MOUSE_SENS_Y, XR64_PC_MOUSE_INVERT_X,
    XR64_PC_MOUSE_INVERT_Y, XR64_PC_MOUSE_RESET};
static const uint32_t s_pc_detail[] = {XR64_PC_BIND_TITLE, XR64_PC_BIND_PRIMARY, XR64_PC_BIND_ALTERNATE,
    XR64_PC_BIND_CLEAR_PRIMARY, XR64_PC_BIND_CLEAR_ALTERNATE};
static const uint32_t s_pc_players[] = {XR64_PC_PLAYER_1, XR64_PC_PLAYER_2, XR64_PC_PLAYER_3,
    XR64_PC_PLAYER_4, XR64_PC_PLAYERS_RESET};
static const uint32_t s_vr_home[] = {XR64_PC_VR_ACTION, XR64_PC_VR_STATUS, XR64_PC_VR_STARTUP, XR64_PC_XR_MOTION,
    XR64_PC_OPEN_XR_BINDINGS, XR64_PC_OPEN_VR_MOVE, XR64_PC_OPEN_VR_VIEW, XR64_PC_XR_RECENTER};
static const uint32_t s_vr_move[] = {XR64_PC_XR_MOVE_DIRECTION, XR64_PC_XR_ANALOG_MOVE, XR64_PC_XR_TURN,
    XR64_PC_XR_MOVE, XR64_PC_XR_TRIGGER};
static const uint32_t s_vr_view[] = {XR64_PC_XR_HUD_DISTANCE, XR64_PC_XR_HUD_SIZE, XR64_PC_XR_MENU_DISTANCE,
    XR64_PC_XR_MENU_SIZE, XR64_PC_XR_RETICLE};
/* Binding order shown to the owner: movement/fire first, then camera/menu. */
static const unsigned s_binding_actions[19] = {8,9,10,18,11,12,13,0,1,2,3,4,5,6,7,14,15,16,17};

static unsigned page_items(unsigned page, const uint32_t** items) {
    static uint32_t bindings[20];
    unsigned i;
#define XR64_PAGE(list) do { *items = list; return (unsigned)(sizeof(list) / sizeof(list[0])); } while (0)
    switch (page) {
    case PC_DISPLAY: XR64_PAGE(s_pc_display);
    case PC_CONTROLS: XR64_PAGE(s_pc_controls);
    case PC_CONTROLLER: XR64_PAGE(s_pc_controller);
    case PC_MOUSE: XR64_PAGE(s_pc_mouse);
    case PC_BIND_DETAIL: XR64_PAGE(s_pc_detail);
    case PC_VR: XR64_PAGE(s_vr_home);
    case PC_VR_MOVE: XR64_PAGE(s_vr_move);
    case PC_VR_VIEW: XR64_PAGE(s_vr_view);
    case PC_PLAYERS: XR64_PAGE(s_pc_players);
    case PC_BINDINGS:
        for (i = 0; i < 19; ++i) bindings[i] = XR64_PC_BIND_ACTION_BASE + s_binding_actions[i];
        bindings[19] = s_binding_device == 2 ? XR64_PC_XR_RESET : s_binding_device ? XR64_PC_PAD_RESET : XR64_PC_RESET_KEYS;
        *items = bindings;
        return 20;
    default: XR64_PAGE(s_pc_home);
    }
#undef XR64_PAGE
}

typedef struct RowSnapshot {
    uint32_t id;
    uint32_t node;
    uint32_t text;
    uint32_t label;
    uint32_t up;
    uint32_t down;
    int16_t y;
    int16_t text_x;
    int16_t text_width;
} RowSnapshot;

/* Native rows of each Options page in on-screen order, then the button roles. */
static const uint32_t s_pause_native_ids[] = {0x377, 0x37C, 0x375, 0x378, 0x37A, 0x380, 0x37D, 0x37E, 0x37F};
static const uint32_t s_pause_button_ids[] = {0x377, 0x37C, 0x380, 0x37D};
static const uint32_t s_title_native_ids[] = {0x57, 0x58, 0x59, 0x55, 0x51, 0x53, 0x50};
static const uint32_t s_title_button_ids[] = {0x57, 0x58};
static const uint32_t* s_native_ids = s_pause_native_ids;
static unsigned s_native_count = 9;
static const uint32_t* s_button_ids = s_pause_button_ids;
static unsigned s_button_count = 4;
static uint32_t s_back_id = 0x37F;
static uint32_t s_gateway_anchor = 0x380;
static unsigned s_extra_count;
static int s_title_options;

static RowSnapshot s_rows[XR64_PC_MAX_NODES];
static unsigned s_row_count;
/* Page rows: node index into s_rows; s_visible_option per on-screen position. */
static unsigned s_page_nodes[XR64_PC_MAX_ROWS];
static unsigned s_page_rows;
static uint32_t s_visible_option[XR64_PC_MAX_ROWS];
static unsigned s_visible_node[XR64_PC_MAX_ROWS];
static unsigned s_visible_count;
static uint32_t s_entry_gateway;
enum { XR64_PC_MORE = 0xFFFF };

static uint32_t page16_find(uint8_t* rdram, recomp_context* ctx,
                            uint32_t root, uint32_t id) {
    ctx->r4 = root;
    ctx->r5 = id;
    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
    return (uint32_t)ctx->r2;
}

static void copy_words(uint8_t* rdram, uint32_t destination,
                       uint32_t source, uint32_t size) {
    uint32_t offset;
    for (offset = 0; offset < size; offset += 4) {
        MEM_W(offset, destination) = MEM_W(offset, source);
    }
}

static uint32_t button_text(uint8_t* rdram, uint32_t wrapper) {
    uint32_t source = wrapper != 0 ? MEM_W(0x8, wrapper) : 0;
    return source != 0 ? MEM_W(0x8, source) : 0;
}

static void write_guest_string(uint8_t* rdram, uint32_t destination,
                               const char* source) {
    uint32_t i = 0;
    while (i + 1 < XR64_PC_OPTIONS_LABEL_CAPACITY && source[i] != '\0') {
        MEM_B(i, destination) = source[i];
        ++i;
    }
    while (i < XR64_PC_OPTIONS_LABEL_CAPACITY) {
        MEM_B(i, destination) = 0;
        ++i;
    }
}

static unsigned injected_count(void) { return XR64_PC_GATEWAYS + s_extra_count; }
static uint32_t injected_source(unsigned i) { return s_pc_options_block + i * XR64_PC_SOURCE_STRIDE; }
static uint32_t gateway_label_address(unsigned gateway) {
    return s_pc_options_block + injected_count() * XR64_PC_SOURCE_STRIDE +
            gateway * XR64_PC_OPTIONS_LABEL_CAPACITY;
}
static uint32_t option_label_address(uint32_t position) {
    return gateway_label_address(XR64_PC_GATEWAYS) + position * XR64_PC_OPTIONS_LABEL_CAPACITY;
}
static uint32_t block_size_for(unsigned extras) {
    return (XR64_PC_GATEWAYS + extras) * XR64_PC_SOURCE_STRIDE +
            (XR64_PC_GATEWAYS + XR64_PC_MAX_ROWS) * XR64_PC_OPTIONS_LABEL_CAPACITY;
}
static const char* gateway_name(unsigned gateway) { return gateway ? "VR OPTIONS" : "PC OPTIONS"; }

/* Move native focus off a row that is about to be hidden. */
static void focus_node(uint8_t* rdram, uint32_t node, int force) {
    uint32_t manager, index, stack, current;
    unsigned i;
    int ours = 0;
    manager = (uint32_t)MEM_W(0x80144E00, 0);
    if (manager < 0x80000000U || manager > 0x807FFFF0U || (manager & 3U) || node == 0) return;
    index = (uint32_t)MEM_W(4, manager);
    stack = (uint32_t)MEM_W(0xC, manager);
    if (index >= 32 || stack < 0x80000000U || (stack & 3U) || stack + (index + 1) * 0x1C > 0x80800000U) return;
    current = (uint32_t)MEM_W(index * 0x1C + 8, stack);
    for (i = 0; i < s_row_count; ++i) if (s_rows[i].node == current) ours = 1;
    if (!ours || current == node) return;
    if (!force)
        for (i = 0; i < s_visible_count; ++i) if (s_rows[s_visible_node[i]].node == current) return;
    MEM_W(index * 0x1C + 8, stack) = node;
}

static void layout_page(uint8_t* rdram) {
    const uint32_t* items;
    const unsigned count = page_items(s_pc_page, &items);
    const unsigned capacity = s_page_rows - 1;          /* rows before BACK */
    const int paged = count > capacity;
    const unsigned per_screen = paged ? capacity - 1 : count;
    unsigned* offset = s_pc_page == PC_BINDINGS ? &s_binding_offset : &s_page_offset;
    unsigned i, shown, spacing;
    if (*offset >= count) *offset = 0;
    shown = 0;
    for (i = 0; i < per_screen && *offset + i < count; ++i) {
        s_visible_option[shown] = items[*offset + i];
        s_visible_node[shown] = s_page_nodes[shown];
        ++shown;
    }
    if (paged) {
        s_visible_option[shown] = XR64_PC_MORE;
        s_visible_node[shown] = s_page_nodes[shown];
        ++shown;
    }
    s_visible_option[shown] = XR64_PC_BACK;
    s_visible_node[shown] = s_page_nodes[s_page_rows - 1];
    ++shown;
    s_visible_count = shown;
    spacing = s_page_rows > 1 ? ((unsigned)s_parent_height - XR64_PC_ROW_HEIGHT) / (s_page_rows - 1) : 15;
    if (spacing > 15) spacing = 15;
    for (i = 0; i < s_page_rows; ++i) MEM_H(0x16, s_rows[s_page_nodes[i]].node) = 300;
    for (i = 0; i < shown; ++i) {
        RowSnapshot* row = &s_rows[s_visible_node[i]];
        const uint32_t address = option_label_address(i);
        MEM_H(0x16, row->node) = (uint16_t)(i * spacing);
        MEM_W(0x2C, row->node) = s_rows[s_visible_node[(i + shown - 1) % shown]].node;
        MEM_W(0x30, row->node) = s_rows[s_visible_node[(i + 1) % shown]].node;
        MEM_H(0x14, row->text) = 6;
        MEM_H(0x18, row->text) = 168;
        if (s_visible_option[i] == XR64_PC_MORE) {
            char more[] = "MORE  1/1";
            const unsigned pages = (count + per_screen - 1) / per_screen;
            more[6] = (char)('1' + (*offset / per_screen) % 9);
            more[8] = (char)('0' + pages % 10);
            write_guest_string(rdram, address, more);
        } else {
            write_guest_string(rdram, address, xr64_pc_option_label(s_visible_option[i]));
        }
        MEM_W(0x38, row->text) = address;
    }
    focus_node(rdram, s_rows[s_visible_node[0]].node, 0);
}

static void leave_pc_options(uint8_t* rdram) {
    uint32_t i;
    if (!s_pc_options_active) return;
    xr64_pc_binding_cancel();
    focus_node(rdram, s_entry_gateway, 1);
    for (i = 0; i < s_row_count; ++i) {
        if (s_rows[i].node == 0) continue;
        MEM_H(0x16, s_rows[i].node) = (uint16_t)s_rows[i].y;
        MEM_W(0x2C, s_rows[i].node) = s_rows[i].up;
        MEM_W(0x30, s_rows[i].node) = s_rows[i].down;
        if (s_rows[i].text != 0) {
            MEM_H(0x14, s_rows[i].text) = (uint16_t)s_rows[i].text_x;
            MEM_H(0x18, s_rows[i].text) = (uint16_t)s_rows[i].text_width;
            MEM_W(0x38, s_rows[i].text) = s_rows[i].label;
        }
    }
    for (i = 0; i < XR64_PC_GATEWAYS; ++i) write_guest_string(rdram, gateway_label_address(i), gateway_name(i));
    s_pc_options_active = 0;
    s_pc_page = 0;
    xr64_native_menu_trace("pc_options_subpage_leave", rdram,
                           s_pc_options_wrapper, s_page16_root, 0);
}

static int snapshot_row(uint8_t* rdram, recomp_context* ctx, uint32_t id) {
    RowSnapshot* row = &s_rows[s_row_count];
    row->id = id;
    row->node = page16_find(rdram, ctx, s_page16_root, id);
    if (row->node == 0) return -1;
    row->y = (int16_t)MEM_H(0x16, row->node);
    row->up = MEM_W(0x2C, row->node);
    row->down = MEM_W(0x30, row->node);
    row->text = button_text(rdram, row->node);
    if (row->text != 0) {
        row->label = MEM_W(0x38, row->text);
        row->text_x = (int16_t)MEM_H(0x14, row->text);
        row->text_width = (int16_t)MEM_H(0x18, row->text);
    }
    return (int)s_row_count++;
}

static int enter_pc_options(uint8_t* rdram, recomp_context* ctx, unsigned gateway) {
    uint32_t i;
    int index;
    unsigned rows;
    if (s_pc_options_active || s_pc_options_block == 0 || s_page16_root == 0 || s_row_parent == 0) {
        return 0;
    }
    s_row_count = 0;
    s_page_rows = 0;
    s_parent_height = (int16_t)MEM_H(0x1A, s_row_parent);
    rows = s_parent_height > XR64_PC_ROW_HEIGHT ? ((unsigned)s_parent_height - XR64_PC_ROW_HEIGHT) / 12 + 1 : 0;
    if (rows > XR64_PC_MAX_ROWS) rows = XR64_PC_MAX_ROWS;
    if (rows < 4) return 0;
    for (i = 0; i < s_native_count; ++i) if (snapshot_row(rdram, ctx, s_native_ids[i]) < 0) return 0;
    /* Page rows: native buttons, both gateways, then extras; BACK is the native Back. */
    for (i = 0; i < s_button_count && s_page_rows + 1 < rows; ++i) {
        unsigned j;
        for (j = 0; j < s_row_count; ++j) if (s_rows[j].id == s_button_ids[i]) s_page_nodes[s_page_rows++] = j;
    }
    for (i = 0; i < injected_count(); ++i) {
        index = snapshot_row(rdram, ctx, XR64_PC_OPTIONS_ID + i);
        if (index < 0) return 0;
        if (s_page_rows + 1 < rows) s_page_nodes[s_page_rows++] = (unsigned)index;
    }
    for (i = 0; i < s_row_count; ++i) {
        if (s_rows[i].id == s_back_id) s_page_nodes[s_page_rows++] = i;
    }
    if (s_page_rows != rows) return 0;
    for (i = 0; i < s_page_rows; ++i) {
        if (s_rows[s_page_nodes[i]].text == 0) return 0;
    }
    s_entry_gateway = s_rows[s_native_count + gateway].node;
    s_pc_options_active = 1;
    s_pc_page = gateway ? PC_VR : PC_HOME;
    s_page_offset = s_binding_offset = 0;
    for (i = 0; i < s_row_count; ++i) MEM_H(0x16, s_rows[i].node) = 300;
    layout_page(rdram);
    xr64_native_menu_trace("pc_options_subpage_enter", rdram,
                           s_pc_options_wrapper, s_page16_root, gateway);
    return 1;
}

/*
 * Create one owned allocation holding both gateway sources, the off-screen
 * extra sources and every label, link the sources under the row parent and wrap
 * each through the native B61 wrapper (0041B190). Gateways are inserted after
 * the anchor row; every visible native row is relaid out at a uniform spacing
 * that keeps the list inside the panel (growing the row parent by at most 15).
 */
static int add_buttons(uint8_t* rdram, recomp_context* ctx, uint32_t root, uint32_t template_wrapper,
                       int16_t height_allowance) {
    uint32_t template_source, template_text, template_tail, parent, sibling, block, i;
    uint32_t order[16];
    unsigned visible = 0, inserted = 0;
    int16_t first_y = 0, height;
    unsigned spacing;
    template_source = MEM_W(0x8, template_wrapper);
    template_text = template_source != 0 ? MEM_W(0x8, template_source) : 0;
    template_tail = template_text != 0 ? MEM_W(0x4, template_text) : 0;
    parent = MEM_W(0x0, template_wrapper);
    if (template_source == 0 || template_text == 0 || template_tail == 0 || parent == 0) {
        xr64_native_menu_trace("pc_options_row_bad_template", rdram,
                               template_source, template_text, template_tail);
        return 0;
    }
    s_extra_count = XR64_PC_MAX_ROWS - s_button_count - XR64_PC_GATEWAYS - 1;
    s_pc_options_block_size = block_size_for(s_extra_count);
    ctx->r4 = s_pc_options_block_size;
    resident_wave14_func_00252FEC(rdram, ctx);
    block = (uint32_t)ctx->r2;
    if (block == 0) {
        xr64_native_menu_trace("pc_options_row_alloc_failed", rdram, s_pc_options_block_size, 0, 0);
        return 0;
    }
    s_pc_options_block = block;
    s_page16_root = root;
    s_row_parent = parent;
    for (i = 0; i < injected_count(); ++i) {
        const uint32_t source = injected_source(i);
        const uint32_t text = source + XR64_PC_OPTIONS_TEXT_OFFSET;
        const uint32_t tail = source + XR64_PC_OPTIONS_TAIL_OFFSET;
        const uint32_t label = i < XR64_PC_GATEWAYS ? gateway_label_address(i) : option_label_address(0);
        copy_words(rdram, source, template_source, 0x44);
        copy_words(rdram, text, template_text, 0x3C);
        copy_words(rdram, tail, template_tail, 0x38);
        MEM_W(0x0, source) = parent;
        MEM_W(0x4, source) = 0;
        MEM_W(0x8, source) = text;
        MEM_H(0xC, source) = (uint16_t)(XR64_PC_OPTIONS_ID + i);
        MEM_H(0x16, source) = 300;
        MEM_W(0x2C, source) = 0;
        MEM_W(0x30, source) = 0;
        MEM_W(0x34, source) = 0;
        MEM_W(0x38, source) = 0;
        MEM_W(0x0, text) = source;
        MEM_W(0x4, text) = tail;
        MEM_W(0x8, text) = 0;
        MEM_H(0xC, text) = (uint16_t)-1;
        MEM_W(0x34, text) = 0;
        MEM_W(0x38, text) = label;
        MEM_W(0x0, tail) = source;
        MEM_W(0x4, tail) = 0;
        MEM_W(0x8, tail) = 0;
        MEM_H(0xC, tail) = (uint16_t)-1;
        sibling = MEM_W(0x8, parent);
        if (sibling == 0) {
            MEM_W(0x8, parent) = source;
        } else {
            while (MEM_W(0x4, sibling) != 0) sibling = MEM_W(0x4, sibling);
            MEM_W(0x4, sibling) = source;
        }
    }
    for (i = 0; i < XR64_PC_GATEWAYS; ++i) write_guest_string(rdram, gateway_label_address(i), gateway_name(i));
    write_guest_string(rdram, option_label_address(0), "");
    for (i = 0; i < injected_count(); ++i) {
        ctx->r4 = XR64_PC_OPTIONS_ID + i;
        trace_func_0041B190_r0017F190(rdram, ctx);
        if (page16_find(rdram, ctx, root, XR64_PC_OPTIONS_ID + i) == 0) {
            xr64_native_menu_trace("pc_options_row_wrap_failed", rdram, block, parent, i);
            return 0;
        }
    }
    s_pc_options_wrapper = page16_find(rdram, ctx, root, XR64_PC_OPTIONS_ID);
    /* On-screen order: visible native rows with both gateways after the anchor. */
    for (i = 0; i < s_native_count && visible + XR64_PC_GATEWAYS < 16; ++i) {
        const uint32_t node = page16_find(rdram, ctx, root, s_native_ids[i]);
        if (node == 0 || (int16_t)MEM_H(0x16, node) >= 300) continue;
        if (visible == 0) first_y = (int16_t)MEM_H(0x16, node);
        order[visible++] = node;
        if (s_native_ids[i] == s_gateway_anchor) {
            order[visible++] = page16_find(rdram, ctx, root, XR64_PC_OPTIONS_ID);
            order[visible++] = page16_find(rdram, ctx, root, XR64_PC_OPTIONS_ID + 1);
            inserted = 1;
        }
    }
    if (!inserted || visible < 2) return 0;
    height = (int16_t)MEM_H(0x1A, parent);
    spacing = ((unsigned)(height + height_allowance - first_y) - XR64_PC_ROW_HEIGHT) / (visible - 1);
    if (spacing > 15) spacing = 15;
    for (i = 0; i < visible; ++i) {
        MEM_H(0x16, order[i]) = (uint16_t)(first_y + i * spacing);
        MEM_W(0x2C, order[i]) = order[(i + visible - 1) % visible];
        MEM_W(0x30, order[i]) = order[(i + 1) % visible];
    }
    {
        const int16_t needed = (int16_t)(first_y + (visible - 1) * spacing + XR64_PC_ROW_HEIGHT + 1);
        if (needed > height) MEM_H(0x1A, parent) = (uint16_t)needed;
    }
    return 1;
}

void xr64_page16_add_pc_options(uint8_t* rdram, recomp_context* ctx,
                                uint32_t root) {
    uint32_t sound_wrapper;
    uint32_t i;
    if (s_pc_options_block != 0 || root == 0) {
        return;
    }
    s_title_options = 0;
    s_native_ids = s_pause_native_ids;
    s_native_count = sizeof(s_pause_native_ids) / sizeof(s_pause_native_ids[0]);
    s_button_ids = s_pause_button_ids;
    s_button_count = sizeof(s_pause_button_ids) / sizeof(s_pause_button_ids[0]);
    s_back_id = 0x37F;
    s_gateway_anchor = 0x380;
    for (i = 0; i < XR64_PC_MAX_INJECTED; ++i) {
        if (page16_find(rdram, ctx, root, XR64_PC_OPTIONS_ID + i) != 0) {
            xr64_native_menu_trace("pc_options_row_id_collision", rdram,
                                   XR64_PC_OPTIONS_ID + i, root, 0);
            return;
        }
    }
    sound_wrapper = page16_find(rdram, ctx, root, 0x377);
    if (sound_wrapper == 0 || page16_find(rdram, ctx, root, 0x380) == 0 ||
        page16_find(rdram, ctx, root, 0x37D) == 0) {
        xr64_native_menu_trace("pc_options_row_missing_template", rdram, sound_wrapper, 0, 0);
        return;
    }
    if (!add_buttons(rdram, ctx, root, sound_wrapper, 15)) return;  /* remove handles a partial add */
    xr64_native_menu_trace("pc_options_row_ready", rdram,
                           s_pc_options_wrapper, s_pc_options_block, s_row_parent);
}

/* Title/lobby Options (resource 4): three buttons and four native value widgets. */
void xr64_title_add_pc_options(uint8_t* rdram, recomp_context* ctx) {
    recomp_context work = *ctx;
    uint32_t table, root, sound, back, brightness, template_source, template_text;
    unsigned i;
    if (s_pc_options_block) return;
    table = (uint32_t)MEM_W(0x8014B9B4, 0);
    if (table < 0x80000000U || table > 0x807FFFC0U || (table & 3U)) return;
    root = (uint32_t)MEM_W(4 * 12 + 8, table);
    if (!root) return;
    for (i = 0; i < XR64_PC_MAX_INJECTED; ++i)
        if (page16_find(rdram, &work, root, XR64_PC_OPTIONS_ID + i)) return;
    sound = page16_find(rdram, &work, root, 0x57);
    brightness = page16_find(rdram, &work, root, 0x53);
    back = page16_find(rdram, &work, root, 0x50);
    if (!sound || !brightness || !back || MEM_HU(0xE, sound) != 0xB61) return;
    template_source = MEM_W(8, sound);
    template_text = template_source ? MEM_W(8, template_source) : 0;
    if (!template_text || MEM_HU(0xE, template_text) != 4) return;
    s_native_ids = s_title_native_ids;
    s_native_count = sizeof(s_title_native_ids) / sizeof(s_title_native_ids[0]);
    s_button_ids = s_title_button_ids;
    s_button_count = sizeof(s_title_button_ids) / sizeof(s_title_button_ids[0]);
    s_back_id = 0x50;
    s_gateway_anchor = 0x53;
    s_title_options = 1;
    if (!add_buttons(rdram, &work, root, sound, 0) && !s_pc_options_block) s_title_options = 0;
}

void xr64_title_remove_pc_options(uint8_t* rdram, recomp_context* ctx) {
    recomp_context work = *ctx;
    if (s_title_options) xr64_page16_remove_pc_options(rdram, &work);
}

int xr64_title_handle_selection(uint8_t* rdram, recomp_context* ctx, uint32_t id) {
    recomp_context work = *ctx;
    return s_title_options && xr64_page16_handle_selection(rdram, &work, id);
}

/* Options normally restores +0x64 in its animated Resume callback. Start and
 * main Pause Resume bypass it, and can destroy Options before resuming audio.
 * Retain that same track across either teardown order; never change music while
 * paused or after the game has already selected a different track. */
static uint32_t s_options_gameplay_track;
static int s_options_track_pending;

void xr64_page16_restore_gameplay_music(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t state=(uint32_t)MEM_W(0x800DF178,0);
    if(MEM_BU(0x80140225,0)!=1) {
        s_options_track_pending=0;
        return;
    }
    if(state>=0x80000000U && state<=0x807FFF94U && !(state&3U)) {
        const uint32_t saved=(uint32_t)MEM_W(0x64,state);
        if(saved!=0x34U) {
            s_options_gameplay_track=saved;
            s_options_track_pending=1;
        }
    }
    if(MEM_W(0x801407D4,0)!=0 || !s_options_track_pending)return;
    if(MEM_W(0x80107E0C,0)==0x34 && MEM_W(0x801407D0,0)==0) {
        recomp_context saved_context=*ctx;
        uint32_t track=s_options_gameplay_track;
        if(track==0) {
            resident_wave14_func_002744C4(rdram,ctx);
            track=(uint32_t)((int32_t)ctx->r2%4+0x38);
        }
        ctx->r4=track;
        trace_func_0025E274_r0005EE74(rdram,ctx);
        *ctx=saved_context;
        fprintf(stderr,"RW104_MENU_MUSIC_RESTORE track=0x%X\n",track);
    }
    if(MEM_W(0x80107E0C,0)!=0x34)s_options_track_pending=0;
}

void xr64_page16_remove_pc_options(uint8_t* rdram, recomp_context* ctx) {
    uint32_t wrappers[XR64_PC_MAX_INJECTED];
    unsigned i, found = 0;
    if (!s_title_options) xr64_page16_restore_gameplay_music(rdram,ctx);
    leave_pc_options(rdram);
    if (s_pc_options_block != 0) {
        const uint32_t block = s_pc_options_block;
        recomp_context lookup = *ctx;
        for (i = 0; i < injected_count(); ++i) {
            wrappers[i] = s_page16_root ? page16_find(rdram, &lookup, s_page16_root, XR64_PC_OPTIONS_ID + i) : 0;
            if (wrappers[i] && MEM_HU(0xE, wrappers[i]) != 0xB61) wrappers[i] = 0; /* unwrapped source */
            if (wrappers[i]) ++found;
        }
        /* An unwrapped source is still linked under the row parent: never free it. */
        for (i = 0; i < injected_count(); ++i) if (wrappers[i]) MEM_W(0x8, wrappers[i]) = 0;
        s_pc_options_block = 0;
        s_pc_options_wrapper = 0;
        s_page16_root = 0;
        s_row_parent = 0;
        if (found == injected_count()) {
            ctx->r4 = block;
            resident_wave14_func_00254778(rdram, ctx);
            xr64_native_menu_trace("pc_options_row_source_freed", rdram, block, 0, 0);
        } else {
            xr64_native_menu_trace("pc_options_row_source_retained", rdram, block, found, 0);
        }
    }
    s_title_options = 0;
}

void xr64_page16_refresh_pc_options(uint8_t* rdram) {
    const uint64_t revision=xr64_pc_ui_revision();
    if(!s_pc_options_active || revision==s_ui_revision)return;
    layout_page(rdram);
    s_ui_revision=revision;
}

static unsigned parent_page(unsigned page) {
    switch(page) {
    case PC_BIND_DETAIL: return PC_BINDINGS;
    case PC_BINDINGS: return s_binding_parent;
    case PC_VR_MOVE: case PC_VR_VIEW: return PC_VR;
    default: return PC_HOME;
    }
}

int xr64_page16_handle_back(uint8_t* rdram) {
    if (!s_pc_options_active) return 0;
    xr64_pc_binding_cancel();
    if(s_pc_page==PC_HOME || s_pc_page==PC_VR)leave_pc_options(rdram);
    else {
        s_pc_page=parent_page(s_pc_page);
        if(s_pc_page!=PC_BINDINGS)s_page_offset=0;
        layout_page(rdram);
        xr64_native_menu_trace("pc_options_page",rdram,s_pc_page,0,0);
    }
    return 1;
}

int xr64_page16_handle_selection(uint8_t* rdram,recomp_context* ctx,uint32_t id) {
    uint32_t position;
    if(!s_pc_options_active) {
        if(id==XR64_PC_OPTIONS_ID || id==XR64_PC_OPTIONS_ID+1)
            return enter_pc_options(rdram,ctx,id-XR64_PC_OPTIONS_ID);
        return 0;
    }
    for(position=0;position<s_visible_count;++position) {
        const uint32_t option=s_visible_option[position];
        const unsigned previous=s_pc_page;
        if(id!=s_rows[s_visible_node[position]].id)continue;
        if(option==XR64_PC_BACK)return xr64_page16_handle_back(rdram);
        if(option==XR64_PC_MORE) {
            const uint32_t* items;
            const unsigned count=page_items(s_pc_page,&items);
            unsigned* offset=s_pc_page==PC_BINDINGS ? &s_binding_offset : &s_page_offset;
            *offset+=s_page_rows-2;
            if(*offset>=count)*offset=0;
        } else if(option==XR64_PC_OPEN_KEYBOARD || option==XR64_PC_OPEN_PAD_BINDINGS || option==XR64_PC_OPEN_XR_BINDINGS) {
            s_binding_parent=s_pc_page;
            s_binding_device=option==XR64_PC_OPEN_XR_BINDINGS ? 2 : option==XR64_PC_OPEN_PAD_BINDINGS ? 1 : 0;
            s_binding_offset=0;
            xr64_pc_binding_select(s_binding_device,8);
            s_pc_page=PC_BINDINGS;
        } else if(option>=XR64_PC_BIND_ACTION_BASE && option<=XR64_PC_BIND_ACTION_END) {
            xr64_pc_binding_select(s_binding_device,option-XR64_PC_BIND_ACTION_BASE);
            s_pc_page=PC_BIND_DETAIL;
        } else if(option==XR64_PC_OPEN_VR_VIEW) {
            s_pc_page=PC_VR_VIEW;
        } else if(option==XR64_PC_OPEN_PLAYERS) {
            s_pc_page=PC_PLAYERS;
        } else if(option==XR64_PC_OPEN_VR_MOVE) {
            s_pc_page=PC_VR_MOVE;
        } else if(option==XR64_PC_VR_ACTION) {
            xr64_pc_vr_action();
        } else if(option>=XR64_PC_OPEN_DISPLAY && option<=XR64_PC_OPEN_MOUSE) {
            s_pc_page=option==XR64_PC_OPEN_DISPLAY ? PC_DISPLAY : option==XR64_PC_OPEN_CONTROLS ? PC_CONTROLS
                    : option==XR64_PC_OPEN_CONTROLLER ? PC_CONTROLLER : PC_MOUSE;
        } else {
            xr64_pc_option_cycle(option);
        }
        if(s_pc_page!=previous && s_pc_page!=PC_BINDINGS)s_page_offset=0;
        layout_page(rdram);
        xr64_native_menu_trace("pc_options_selection",rdram,s_pc_page,option,id);
        return 1;
    }
    return 1;
}
