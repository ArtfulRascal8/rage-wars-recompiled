#include "rage_wars_native_pc_options.h"

#include "funcs.h"
#include <stdio.h>

/*
 * The original page-16 resource owns one contiguous layout allocation, while
 * interactive B61 button wrappers are individually allocated. The injected
 * source subtree therefore has a separate guest allocation. It is detached
 * and freed before the page resource is released.
 */
enum {
    XR64_PC_OPTIONS_BLOCK_SIZE = 0x180,
    XR64_PC_OPTIONS_SOURCE_OFFSET = 0x00,
    XR64_PC_OPTIONS_TEXT_OFFSET = 0x44,
    XR64_PC_OPTIONS_TAIL_OFFSET = 0x80,
    XR64_PC_OPTIONS_LABEL_OFFSET = 0xB8,
    XR64_PC_OPTIONS_LABEL_CAPACITY = 0x20,
};

static uint32_t s_pc_options_block;
static uint32_t s_pc_options_wrapper;
static uint32_t s_page16_root;
static int s_pc_options_active;
static unsigned s_pc_page;
/* Six existing button wrappers: five settings/links and Back. No new guest page ID. */
enum { PC_HOME, PC_DISPLAY, PC_CONTROLS, PC_CONTROLLER, PC_MOUSE, PC_RESPONSE, PC_BINDINGS, PC_BIND_DETAIL, PC_VR, PC_VR_MORE, PC_DEVICES };
static unsigned s_binding_device, s_binding_group, s_binding_parent;
static unsigned s_mouse_parent, s_controller_parent;
static uint64_t s_ui_revision;
static const uint32_t s_page_options[6][6] = {
    {XR64_PC_OPEN_DISPLAY, XR64_PC_OPEN_CONTROLS, XR64_PC_OPEN_DEVICES,
     XR64_PC_WEAPON_MODELS, XR64_PC_OPEN_VR, XR64_PC_BACK},
    {XR64_PC_OPTION_RESOLUTION, XR64_PC_OPTION_DISPLAY_MODE, XR64_PC_OPTION_ASPECT_RATIO,
     XR64_PC_OPTION_FOV, XR64_PC_OPTION_MASTER_VOLUME, XR64_PC_BACK},
    {XR64_PC_INPUT_PROFILE, XR64_PC_INPUT_DEVICE, XR64_PC_LOOK_SPRING,
     XR64_PC_OPEN_RESPONSE, XR64_PC_OPEN_KEYBOARD, XR64_PC_BACK},
    {XR64_PC_PAD_SENS_X, XR64_PC_PAD_SENS_Y, XR64_PC_PAD_INVERT_X,
     XR64_PC_PAD_INVERT_Y, XR64_PC_OPEN_PAD_BINDINGS, XR64_PC_BACK},
    {XR64_PC_MOUSE_SENS_X, XR64_PC_MOUSE_SENS_Y, XR64_PC_MOUSE_INVERT_X,
     XR64_PC_MOUSE_INVERT_Y, XR64_PC_MOUSE_RESET, XR64_PC_BACK},
    {XR64_PC_INPUT_CURVE, XR64_PC_AIM_DEADZONE, XR64_PC_MOVE_THRESHOLD,
     XR64_PC_SWAP_STICKS, XR64_PC_PAD_RESET, XR64_PC_BACK},
};
static uint32_t current_option(unsigned slot) {
    static const unsigned actions[19]={8,9,10,18,11,12,13,0,1,2,3,4,5,6,7,14,15,16,17};
    static const uint32_t detail[6]={XR64_PC_BIND_TITLE,XR64_PC_BIND_PRIMARY,
        XR64_PC_BIND_ALTERNATE,XR64_PC_BIND_CLEAR_PRIMARY,XR64_PC_BIND_CLEAR_ALTERNATE,XR64_PC_BACK};
    static const uint32_t vr[6]={XR64_PC_VR_STARTUP,XR64_PC_VR_STATUS,XR64_PC_VR_ACTION,XR64_PC_XR_MOTION,XR64_PC_OPEN_VR_MORE,XR64_PC_BACK};
    static const uint32_t vr_more[6]={XR64_PC_OPEN_XR_BINDINGS,XR64_PC_XR_TURN,XR64_PC_XR_MOVE,XR64_PC_XR_TRIGGER,XR64_PC_XR_RESET,XR64_PC_BACK};
    static const uint32_t devices[6]={XR64_PC_OPEN_CONTROLLER,XR64_PC_OPEN_MOUSE,
        XR64_PC_OPEN_KEYBOARD,XR64_PC_OPEN_XR_BINDINGS,XR64_PC_INPUT_DEVICE,XR64_PC_BACK};
    if(s_pc_page==PC_DEVICES)return devices[slot];
    if(s_pc_page==PC_VR)return vr[slot];
    if(s_pc_page==PC_VR_MORE)return vr_more[slot];
    if(s_pc_page==PC_BIND_DETAIL)return detail[slot];
    if(s_pc_page!=PC_BINDINGS)return s_page_options[s_pc_page][slot];
    if(slot==5)return XR64_PC_BACK;
    if(slot==4)return XR64_PC_NEXT_BINDINGS;
    if(s_binding_group*4+slot<19)return XR64_PC_BIND_ACTION_BASE+actions[s_binding_group*4+slot];
    return s_binding_device==2 ? XR64_PC_XR_RESET : s_binding_device ? XR64_PC_PAD_RESET : XR64_PC_RESET_KEYS;
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

static RowSnapshot s_rows[10];
static const uint32_t s_pause_row_ids[10] = {
    0x377, 0x37C, 0x375, 0x378, 0x37A,
    0x380, XR64_PC_OPTIONS_ID, 0x37D, 0x37E, 0x37F,
};
static const uint32_t s_pause_settings_indices[6] = {0, 1, 5, 6, 7, 9};
static const uint32_t s_title_row_ids[10] = {
    0x57, 0x58, 0x59, 0x55, 0x51, 0x53,
    XR64_PC_OPTIONS_ID, XR64_PC_OPTIONS_ID + 1, XR64_PC_OPTIONS_ID + 2, 0x50,
};
static const uint32_t s_title_settings_indices[6] = {0, 1, 6, 7, 8, 9};
static const uint32_t* s_row_ids = s_pause_row_ids;
static const uint32_t* s_settings_row_indices = s_pause_settings_indices;
static uint32_t s_title_wrappers[3];
static int s_title_options;


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

static void shift_row(uint8_t* rdram, recomp_context* ctx, uint32_t root,
                      uint32_t id, int32_t delta_y) {
    uint32_t node = page16_find(rdram, ctx, root, id);
    if (node != 0) {
        int16_t y = (int16_t)MEM_H(0x16, node);
        MEM_H(0x16, node) = (uint16_t)(y + delta_y);
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

static uint32_t option_label_address(uint32_t option) {
    return s_pc_options_block + XR64_PC_OPTIONS_LABEL_OFFSET +
            option * XR64_PC_OPTIONS_LABEL_CAPACITY;
}

static void refresh_option_label(uint8_t* rdram, uint32_t option) {
    uint32_t snapshot_index = s_settings_row_indices[option];
    uint32_t text = s_rows[snapshot_index].text;
    uint32_t address = option_label_address(option);
    if (text == 0) return;
    write_guest_string(rdram, address, xr64_pc_option_label(current_option(option)));
    if(current_option(option)==XR64_PC_NEXT_BINDINGS) {
        char page[]="PAGE 1/5 - NEXT";
        page[5]=(char)('1'+s_binding_group);
        write_guest_string(rdram,address,page);
    }
    MEM_W(0x38, text) = address;
}

static void leave_pc_options(uint8_t* rdram) {
    uint32_t i;
    if (!s_pc_options_active) return;
    xr64_pc_binding_cancel();
    for (i = 0; i < 10; ++i) {
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
    write_guest_string(rdram, option_label_address(0), "PC OPTIONS");
    s_pc_options_active = 0;
    s_pc_page = 0;
    xr64_native_menu_trace("pc_options_subpage_leave", rdram,
                           s_pc_options_wrapper, s_page16_root, 0);
}

static int enter_pc_options(uint8_t* rdram, recomp_context* ctx) {
    uint32_t i;
    uint32_t visible[6];
    if (s_pc_options_active || s_pc_options_block == 0 || s_page16_root == 0) {
        return 0;
    }
    for (i = 0; i < 10; ++i) {
        RowSnapshot* row = &s_rows[i];
        row->id = s_row_ids[i];
        row->node = page16_find(rdram, ctx, s_page16_root, row->id);
        if (row->node == 0) return 0;
        row->y = (int16_t)MEM_H(0x16, row->node);
        row->up = MEM_W(0x2C, row->node);
        row->down = MEM_W(0x30, row->node);
        row->text = button_text(rdram, row->node);
        if (row->text != 0) {
            row->label = MEM_W(0x38, row->text);
            row->text_x = (int16_t)MEM_H(0x14, row->text);
            row->text_width = (int16_t)MEM_H(0x18, row->text);
        }
    }
    for (i = 0; i < 6; ++i) {
        RowSnapshot* row = &s_rows[s_settings_row_indices[i]];
        if (row->text == 0) return 0;
        visible[i] = row->node;
    }

    s_pc_options_active = 1;
    s_pc_page = 0;
    for (i = 0; i < 10; ++i) {
        MEM_H(0x16, s_rows[i].node) = 300;
    }
    for (i = 0; i < 6; ++i) {
        RowSnapshot* row = &s_rows[s_settings_row_indices[i]];
        MEM_H(0x16, row->node) = (uint16_t)(i == 2 ? 31 : i * 15);
        MEM_W(0x2C, row->node) = visible[(i + 5) % 6];
        MEM_W(0x30, row->node) = visible[(i + 1) % 6];
        MEM_H(0x14, row->text) = 6;
        MEM_H(0x18, row->text) = 168;
    }
    for (i = 0; i < 6; ++i) refresh_option_label(rdram, i);
    xr64_native_menu_trace("pc_options_subpage_enter", rdram,
                           s_pc_options_wrapper, s_page16_root, 0);
    return 1;
}

void xr64_page16_add_pc_options(uint8_t* rdram, recomp_context* ctx,
                                uint32_t root) {
    static const char label[] = "PC OPTIONS";
    uint32_t sound_wrapper;
    uint32_t template_source;
    uint32_t template_text;
    uint32_t template_tail;
    uint32_t bot_wrapper;
    uint32_t resume_wrapper;
    uint32_t parent;
    uint32_t sibling;
    uint32_t source;
    uint32_t text;
    uint32_t tail;
    uint32_t label_address;
    uint32_t i;

    if (s_pc_options_block != 0 || root == 0) {
        return;
    }
    s_title_options = 0;
    s_row_ids = s_pause_row_ids;
    s_settings_row_indices = s_pause_settings_indices;
    s_page16_root = root;

    if (page16_find(rdram, ctx, root, XR64_PC_OPTIONS_ID) != 0) {
        xr64_native_menu_trace("pc_options_row_id_collision", rdram,
                               XR64_PC_OPTIONS_ID, root, 0);
        return;
    }

    sound_wrapper = page16_find(rdram, ctx, root, 0x377);
    bot_wrapper = page16_find(rdram, ctx, root, 0x380);
    resume_wrapper = page16_find(rdram, ctx, root, 0x37D);
    if (sound_wrapper == 0 || bot_wrapper == 0 || resume_wrapper == 0) {
        xr64_native_menu_trace("pc_options_row_missing_template", rdram,
                               sound_wrapper, bot_wrapper, resume_wrapper);
        return;
    }

    template_source = MEM_W(0x8, sound_wrapper);
    template_text = template_source != 0 ? MEM_W(0x8, template_source) : 0;
    template_tail = template_text != 0 ? MEM_W(0x4, template_text) : 0;
    parent = MEM_W(0x0, sound_wrapper);
    if (template_source == 0 || template_text == 0 || template_tail == 0 ||
        parent == 0) {
        xr64_native_menu_trace("pc_options_row_bad_template", rdram,
                               template_source, template_text, template_tail);
        return;
    }

    ctx->r4 = XR64_PC_OPTIONS_BLOCK_SIZE;
    resident_wave14_func_00252FEC(rdram, ctx);
    s_pc_options_block = (uint32_t)ctx->r2;
    if (s_pc_options_block == 0) {
        xr64_native_menu_trace("pc_options_row_alloc_failed", rdram,
                               XR64_PC_OPTIONS_BLOCK_SIZE, 0, 0);
        return;
    }

    source = s_pc_options_block + XR64_PC_OPTIONS_SOURCE_OFFSET;
    text = s_pc_options_block + XR64_PC_OPTIONS_TEXT_OFFSET;
    tail = s_pc_options_block + XR64_PC_OPTIONS_TAIL_OFFSET;
    label_address = s_pc_options_block + XR64_PC_OPTIONS_LABEL_OFFSET;
    copy_words(rdram, source, template_source, 0x44);
    copy_words(rdram, text, template_text, 0x3C);
    copy_words(rdram, tail, template_tail, 0x38);

    MEM_W(0x0, source) = parent;
    MEM_W(0x4, source) = 0;
    MEM_W(0x8, source) = text;
    MEM_H(0xC, source) = XR64_PC_OPTIONS_ID;
    MEM_H(0x16, source) = 88;
    MEM_W(0x2C, source) = bot_wrapper;
    MEM_W(0x30, source) = resume_wrapper;
    MEM_W(0x34, source) = 0;
    MEM_W(0x38, source) = 0;

    MEM_W(0x0, text) = source;
    MEM_W(0x4, text) = tail;
    MEM_W(0x8, text) = 0;
    MEM_H(0xC, text) = (uint16_t)-1;
    MEM_H(0x14, text) = (uint16_t)((int16_t)MEM_H(0x14, text) + 8);
    MEM_W(0x34, text) = 0;
    MEM_W(0x38, text) = label_address;

    MEM_W(0x0, tail) = source;
    MEM_W(0x4, tail) = 0;
    MEM_W(0x8, tail) = 0;
    MEM_H(0xC, tail) = (uint16_t)-1;

    for (i = 0; i < sizeof(label); ++i) {
        MEM_B(i, label_address) = label[i];
    }

    sibling = MEM_W(0x8, parent);
    if (sibling == 0) {
        MEM_W(0x8, parent) = source;
    } else {
        while (MEM_W(0x4, sibling) != 0) {
            sibling = MEM_W(0x4, sibling);
        }
        MEM_W(0x4, sibling) = source;
    }

    ctx->r4 = XR64_PC_OPTIONS_ID;
    trace_func_0041B190_r0017F190(rdram, ctx);
    s_pc_options_wrapper = page16_find(rdram, ctx, root, XR64_PC_OPTIONS_ID);
    if (s_pc_options_wrapper == 0) {
        xr64_native_menu_trace("pc_options_row_wrap_failed", rdram,
                               s_pc_options_block, parent, 0);
        return;
    }

    MEM_W(0x30, bot_wrapper) = s_pc_options_wrapper;
    MEM_W(0x2C, resume_wrapper) = s_pc_options_wrapper;
    shift_row(rdram, ctx, root, 0x37D, 15);
    shift_row(rdram, ctx, root, 0x37E, 15);
    shift_row(rdram, ctx, root, 0x37F, 15);
    MEM_H(0x1A, parent) = (uint16_t)((int16_t)MEM_H(0x1A, parent) + 15);
    xr64_native_menu_trace("pc_options_row_ready", rdram,
                           s_pc_options_wrapper, source, parent);
}

/*
 * Title/lobby Options (resource 4) has three buttons and four native value
 * widgets. Add one visible gateway and two off-screen button wrappers so the
 * six-row PC page can use buttons exclusively. All three sources share one
 * owned allocation; native E03 still owns/free the wrappers. The pre-existing
 * brightness-to-Back gap fits the gateway without resizing the artwork.
 */
void xr64_title_add_pc_options(uint8_t* rdram, recomp_context* ctx) {
    recomp_context work = *ctx;
    uint32_t table, root, sound, back, brightness, template_source;
    uint32_t template_text, template_tail, parent, sibling, block;
    unsigned i;
    if (s_pc_options_block) return;
    table = (uint32_t)MEM_W(0x8014B9B4, 0);
    if (table < 0x80000000U || table > 0x807FFFC0U || (table & 3U)) return;
    root = (uint32_t)MEM_W(4 * 12 + 8, table);
    if (!root) return;
    for (i = 0; i < 3; ++i)
        if (page16_find(rdram, &work, root, XR64_PC_OPTIONS_ID + i)) return;
    sound = page16_find(rdram, &work, root, 0x57);
    brightness = page16_find(rdram, &work, root, 0x53);
    back = page16_find(rdram, &work, root, 0x50);
    if (!sound || !brightness || !back || MEM_HU(0xE, sound) != 0xB61) return;
    template_source = MEM_W(8, sound);
    template_text = template_source ? MEM_W(8, template_source) : 0;
    template_tail = template_text ? MEM_W(4, template_text) : 0;
    parent = MEM_W(0, sound);
    if (!template_source || !template_text || !template_tail || !parent ||
        MEM_HU(0xE, template_text) != 4) return;
    work.r4 = 3 * XR64_PC_OPTIONS_BLOCK_SIZE;
    resident_wave14_func_00252FEC(rdram, &work);
    block = (uint32_t)work.r2;
    if (!block) return;
    s_title_options = 1;
    s_row_ids = s_title_row_ids;
    s_settings_row_indices = s_title_settings_indices;
    s_pc_options_block = block;
    s_page16_root = root;
    for (i = 0; i < 3; ++i) {
        uint32_t source = block + i * XR64_PC_OPTIONS_BLOCK_SIZE;
        uint32_t text = source + XR64_PC_OPTIONS_TEXT_OFFSET;
        uint32_t tail = source + XR64_PC_OPTIONS_TAIL_OFFSET;
        copy_words(rdram, source, template_source, 0x44);
        copy_words(rdram, text, template_text, 0x3C);
        copy_words(rdram, tail, template_tail, 0x38);
        MEM_W(0, source) = parent; MEM_W(4, source) = 0; MEM_W(8, source) = text;
        MEM_H(0xC, source) = (uint16_t)(XR64_PC_OPTIONS_ID + i);
        MEM_H(0x16, source) = i == 0 ? 103 : 300;
        MEM_W(0x2C, source) = brightness; MEM_W(0x30, source) = back;
        MEM_W(0x34, source) = 0; MEM_W(0x38, source) = 0;
        MEM_W(0, text) = source; MEM_W(4, text) = tail; MEM_W(8, text) = 0;
        MEM_H(0xC, text) = (uint16_t)-1;
        MEM_W(0x34, text) = 0;
        MEM_W(0x38, text) = source + XR64_PC_OPTIONS_LABEL_OFFSET;
        write_guest_string(rdram, source + XR64_PC_OPTIONS_LABEL_OFFSET, i == 0 ? "PC OPTIONS" : "");
        MEM_W(0, tail) = source; MEM_W(4, tail) = 0; MEM_W(8, tail) = 0;
        MEM_H(0xC, tail) = (uint16_t)-1;
        sibling = MEM_W(8, parent);
        if (!sibling) MEM_W(8, parent) = source;
        else {
            while (MEM_W(4, sibling)) sibling = MEM_W(4, sibling);
            MEM_W(4, sibling) = source;
        }
        work.r4 = XR64_PC_OPTIONS_ID + i;
        trace_func_0041B190_r0017F190(rdram, &work);
        s_title_wrappers[i] = page16_find(rdram, &work, root, XR64_PC_OPTIONS_ID + i);
    }
    s_pc_options_wrapper = s_title_wrappers[0];
    if (s_title_wrappers[0] && s_title_wrappers[1] && s_title_wrappers[2]) {
        MEM_W(0x30, brightness) = s_pc_options_wrapper;
        MEM_W(0x2C, back) = s_pc_options_wrapper;
    }
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
    if (!s_title_options) xr64_page16_restore_gameplay_music(rdram,ctx);
    leave_pc_options(rdram);
    if (s_title_options) {
        unsigned i;
        for (i = 0; i < 3; ++i) {
            if (s_title_wrappers[i]) MEM_W(0x8, s_title_wrappers[i]) = 0;
            s_title_wrappers[i] = 0;
        }
        s_title_options = 0;
    }
    if (s_pc_options_wrapper != 0) {
        MEM_W(0x8, s_pc_options_wrapper) = 0;
    }
    if (s_pc_options_block != 0) {
        uint32_t block = s_pc_options_block;
        s_pc_options_block = 0;
        s_pc_options_wrapper = 0;
        s_page16_root = 0;
        ctx->r4 = block;
        resident_wave14_func_00254778(rdram, ctx);
        xr64_native_menu_trace("pc_options_row_source_freed", rdram,
                               block, 0, 0);
    }
}

void xr64_page16_refresh_pc_options(uint8_t* rdram) {
    uint32_t i;
    const uint64_t revision=xr64_pc_ui_revision();
    if(!s_pc_options_active || revision==s_ui_revision)return;
    for(i=0;i<6;++i)refresh_option_label(rdram,i);
    s_ui_revision=revision;
}

int xr64_page16_handle_back(uint8_t* rdram) {
    uint32_t i;
    if (!s_pc_options_active) return 0;
    xr64_pc_binding_cancel();
    if(s_pc_page==PC_HOME)leave_pc_options(rdram);
    else {
        if(s_pc_page==PC_BIND_DETAIL)s_pc_page=PC_BINDINGS;
        else if(s_pc_page==PC_BINDINGS)s_pc_page=s_binding_parent;
        else if(s_pc_page==PC_RESPONSE)s_pc_page=PC_CONTROLS;
        else if(s_pc_page==PC_MOUSE)s_pc_page=s_mouse_parent;
        else if(s_pc_page==PC_CONTROLLER)s_pc_page=s_controller_parent;
        else if(s_pc_page==PC_VR_MORE)s_pc_page=PC_VR;
        else s_pc_page=PC_HOME;
        for(i=0;i<6;++i)refresh_option_label(rdram,i);
        xr64_native_menu_trace("pc_options_page",rdram,s_pc_page,0,0);
    }
    return 1;
}

int xr64_page16_handle_selection(uint8_t* rdram,recomp_context* ctx,uint32_t id) {
    uint32_t slot;
    if(!s_pc_options_active)return id==XR64_PC_OPTIONS_ID ? enter_pc_options(rdram,ctx) : 0;
    for(slot=0;slot<6;++slot) {
        uint32_t option,i;
        if(id!=s_row_ids[s_settings_row_indices[slot]])continue;
        option=current_option(slot);
        if(option==XR64_PC_BACK)return xr64_page16_handle_back(rdram);
        if(option==XR64_PC_OPEN_KEYBOARD || option==XR64_PC_OPEN_PAD_BINDINGS || option==XR64_PC_OPEN_XR_BINDINGS) {
            s_binding_parent=s_pc_page;
            s_binding_device=option==XR64_PC_OPEN_XR_BINDINGS ? 2 : option==XR64_PC_OPEN_PAD_BINDINGS ? 1 : 0;
            s_binding_group=0;
            xr64_pc_binding_select(s_binding_device,8);
            s_pc_page=PC_BINDINGS;
        } else if(option==XR64_PC_NEXT_BINDINGS) {
            s_binding_group=(s_binding_group+1)%5;
        } else if(option>=XR64_PC_BIND_ACTION_BASE && option<=XR64_PC_BIND_ACTION_END) {
            xr64_pc_binding_select(s_binding_device,option-XR64_PC_BIND_ACTION_BASE);
            s_pc_page=PC_BIND_DETAIL;
        } else if(option==XR64_PC_OPEN_DEVICES) {
            s_pc_page=PC_DEVICES;
        } else if(option==XR64_PC_OPEN_VR) {
            s_pc_page=PC_VR;
        } else if(option==XR64_PC_OPEN_VR_MORE) {
            s_pc_page=PC_VR_MORE;
        } else if(option==XR64_PC_VR_ACTION) {
            xr64_pc_vr_action();
        } else if(option==XR64_PC_OPEN_RESPONSE) {
            s_pc_page=PC_RESPONSE;
        } else if(option>=XR64_PC_OPEN_DISPLAY && option<=XR64_PC_OPEN_MOUSE) {
            if(option==XR64_PC_OPEN_MOUSE)s_mouse_parent=s_pc_page;
            if(option==XR64_PC_OPEN_CONTROLLER)s_controller_parent=s_pc_page;
            s_pc_page=option-XR64_PC_OPEN_DISPLAY+1;
        } else {
            xr64_pc_option_cycle(option);
        }
        for(i=0;i<6;++i)refresh_option_label(rdram,i);
        xr64_native_menu_trace("pc_options_selection",rdram,s_pc_page,option,id);
        return 1;
    }
    return 1;
}

/* Native focus accessor 00299948 reads manager -> stack[index].focus. Prefix
 * text under that exact focus owner while paused. Root pause (page 1) uses
 * unwrapped type-9 rows; Options uses B61 wrappers. The native glyph builder
 * consumes the bytes immediately (0040D9F0) into frame-owned glyph records.
 * Use its frame allocator, never a host pointer or persistent guest scratch. */
static int focus_span(uint32_t address, uint32_t size) {
    return address >= 0x80000000U && size <= 0x00800000U &&
        address - 0x80000000U <= 0x00800000U - size;
}
uint32_t xr64_pause_focus_text(uint8_t* rdram, recomp_context* ctx,
                              uint32_t node, uint32_t text) {
    uint32_t manager, stack, index, focus, ancestor, size, destination;
    recomp_context allocation;
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
    for (size = 0; size < 125; ++size) {
        if (!focus_span(text + size, 1)) return text;
        if (MEM_BU(size, text) == 0) break;
    }
    if (size == 0 || size == 125) return text;
    allocation = *ctx;
    allocation.r4 = size + 3;
    trace_func_002A1524_r000A2124(rdram, &allocation);
    destination = (uint32_t)allocation.r2;
    if (!focus_span(destination, size + 3)) return text;
    MEM_B(0, destination) = '>';
    MEM_B(1, destination) = ' ';
    for (index = 0; index <= size; ++index) MEM_B(index + 2, destination) = MEM_BU(index, text);
    return destination;
}
