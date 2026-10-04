/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The LionsOS compositor.
 *
 * The compositor is the only client of the GPU and input virtualisers.
 * Applications are separate protection domains, each with a fixed slot (see
 * gui_config.h and <lions/gui/protocol.h>): they draw into their own surface,
 * which the compositor can only read, and receive input only through their
 * own event queue.
 *
 * The compositor keeps a scene of wallpaper, taskbar, application windows in
 * z-order and a software cursor, and software-renders it into its GPU data
 * region, which is scanned out as a 2D resource. Every change marks the rows
 * it touches as damaged; once all pending work has been handled only the
 * damaged band is redrawn and sent to the device.
 *
 * The compositor is also the shell: a taskbar with a tab per open window, a
 * launcher menu behind the Lions button listing every application, and
 * Alt+Tab to cycle through open windows. A window is either open (visible),
 * minimised (open but hidden, dimmed tab) or closed (no tab, reopened from
 * the launcher).
 *
 * Input routing: keys go to the focused (topmost) window. Pressing a button
 * over a window's content focuses it and grabs the pointer for that window
 * until the button is released. Pointer motion goes to the grabbing window,
 * or to the focused window while the pointer is over its content. Title bars,
 * close boxes and the taskbar are handled by the compositor itself.
 *
 * Layout of the GPU data region:
 *   [0, FB_OFFSET)          display info written by the virtualiser
 *   [FB_OFFSET, ...)        framebuffer, attached as the resource backing
 */

#include <stdint.h>
#include <stddef.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/gpu/queue.h>
#include <sddf/gpu/events.h>
#include <lions/input/input.h>
#include <lions/gui/protocol.h>
#include <gpu_config.h>
#include <input_config.h>
#include <gui_config.h>
#include "gfx.h"

#define LOG_COMPOSITOR(...) do{ sddf_dprintf("COMPOSITOR|INFO: "); sddf_dprintf(__VA_ARGS__); }while(0)
#define LOG_COMPOSITOR_ERR(...) do{ sddf_dprintf("COMPOSITOR|ERROR: "); sddf_dprintf(__VA_ARGS__); }while(0)

/* Fixed by meta.py */
#define VIRT_CH 0
#define INPUT_CH 2
#define APP_CH(slot) (GUI_APP_CH_BASE + (slot))

#define DISPLAY_INFO_OFFSET 0
#define FB_OFFSET 0x1000
#define FB_MAX_BYTES (GPU_DATA_REGION_SIZE_CLI0 - FB_OFFSET)

#define SCANOUT_ID 0
#define FB_RESOURCE_ID 1

/* Limits on what an app may ask for. The cap is the protocol's, so that a size
   gui_size_clamp() hands out is never one this file rejects. */
#define APP_MAX_DIMENSION GUI_MAX_DIMENSION

/* Theme */
#define COLOUR_BG_TOP GFX_RGB(0x14, 0x2a, 0x4f)
#define COLOUR_BG_BOTTOM GFX_RGB(0x2e, 0x7d, 0x8c)
#define COLOUR_BG_LOGO GFX_RGB(0x9f, 0xc4, 0xd1)
#define COLOUR_BG_HINT GFX_RGB(0x7f, 0xa8, 0xb8)
#define COLOUR_TASKBAR GFX_RGB(0x1b, 0x1f, 0x27)
#define COLOUR_TASKBAR_EDGE GFX_RGB(0x3a, 0x41, 0x4f)
#define COLOUR_TAB_ACTIVE GFX_RGB(0x33, 0x3a, 0x48)
#define COLOUR_BUTTON GFX_RGB(0xe0, 0x8a, 0x1e)
#define COLOUR_TEXT_LIGHT GFX_RGB(0xf2, 0xf2, 0xf2)
#define COLOUR_TEXT_TAB GFX_RGB(0xb8, 0xbe, 0xc9)
#define COLOUR_TEXT_TAB_HIDDEN GFX_RGB(0x6b, 0x72, 0x80)
#define COLOUR_TEXT_DARK GFX_RGB(0x22, 0x22, 0x22)
#define COLOUR_WINDOW_BORDER GFX_RGB(0x0f, 0x14, 0x1c)
#define COLOUR_TITLE_ACTIVE GFX_RGB(0x2f, 0x5d, 0xa8)
#define COLOUR_TITLE_INACTIVE GFX_RGB(0x6b, 0x72, 0x80)
#define COLOUR_SHADOW GFX_RGB(0x0a, 0x12, 0x20)
#define COLOUR_CLOSE GFX_RGB(0xd9, 0x4c, 0x3d)
#define COLOUR_CURSOR_EDGE GFX_RGB(0x00, 0x00, 0x00)
#define COLOUR_CURSOR_FILL GFX_RGB(0xff, 0xff, 0xff)

#define TASKBAR_HEIGHT 36
#define TITLE_HEIGHT 24
#define BORDER 1
#define SHADOW_OFFSET 6
#define TEXT_SCALE 2
#define CHAR_HEIGHT (GFX_FONT_HEIGHT * TEXT_SCALE)
#define CLOSE_SIZE 12
/* Corner grab area for resizing */
#define RESIZE_HANDLE 12
#define MENU_ITEM_HEIGHT 28
#define MENU_PADDING 6
#define COLOUR_MENU GFX_RGB(0x24, 0x29, 0x33)
#define COLOUR_MENU_HOVER GFX_RGB(0x2f, 0x5d, 0xa8)
#define COLOUR_MENU_HEADER GFX_RGB(0x8a, 0x8f, 0x99)

gpu_events_t *gpu_events;
gpu_req_queue_t *gpu_req_queue;
gpu_resp_queue_t *gpu_resp_queue;
uintptr_t gpu_data;
uintptr_t input_queue;
uintptr_t gui_surfaces;
uintptr_t gui_states;
uintptr_t gui_events;

static gpu_queue_handle_t gpu_queue_handle;
static input_queue_handle_t input_handle;
static uint32_t next_req_id = 0;
static uint32_t display_info_req_id = UINT32_MAX;
static bool display_info_pending = false;
static bool running = false;

static gpu_rect_t scanout_rect;
static gfx_surface_t screen;

/* Rows [dirty_y0, dirty_y1) need to be redrawn and presented */
static int32_t dirty_y0 = INT32_MAX;
static int32_t dirty_y1 = 0;

/* Application slots */
typedef struct app {
    /* Shared regions, see gui_config.h */
    const volatile gui_state_t *state;
    const uint32_t *pixels;
    gui_event_queue_t *events;

    /* Validated copies of what the app published */
    int32_t width;
    int32_t height;
    char title[GUI_TITLE_MAX];
    uint32_t seen_seq;
    /* The app says it can adopt a new size, so offer a corner to drag */
    bool resizable;

    /* The app has published a valid surface and has a window */
    bool mapped;
    /* Open windows have a taskbar tab; visible ones are also on screen */
    bool open;
    bool visible;
    /* Top left of the window frame on screen */
    int32_t x;
    int32_t y;
    gfx_rect_t tab;
} app_t;

static app_t apps[GUI_NUM_APPS];
/* Slots from back to front */
static int z_order[GUI_NUM_APPS];
static int focused = -1;
/* Apps that have been sent events since they were last notified */
static uint32_t pending_notify;

/* Pointer state */
static int32_t pointer_x;
static int32_t pointer_y;
static int drag_slot = -1;
static int32_t drag_dx, drag_dy;
/*
 * Slot whose frame is showing a size the app has not adopted yet, and that size.
 * Cleared when the app commits, so the window cannot stay letterboxed.
 */
static int resize_slot = -1;
static int32_t resize_w, resize_h;
/* A corner drag is live, so motion changes the size rather than moving */
static bool resizing;
static int grab_slot = -1;
static bool alt_left, alt_right;

/* Launcher menu */
static bool menu_open;
static int menu_hover = -1;

/* Motion reported by the current input frame, applied at SYN_REPORT */
static bool motion_pending;
static int32_t next_pointer_x, next_pointer_y;

/* Classic arrow cursor: 'X' is the outline, '.' the fill, hot spot top left */
static const char *const cursor_image[] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX",
    "X...X..X    ", "X..XX..X    ", "X.X  X..X   ", "XX   X..X   ", "X     X..X  ", "      X..X  ",
    "       XX   ",
};
#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT ((int32_t)ARRAY_SIZE(cursor_image))

/* GPU */

static uint32_t new_req_id(void)
{
    return next_req_id++;
}

static void enqueue(gpu_req_t req)
{
    int err = gpu_enqueue_req(&gpu_queue_handle, req);
    if (err) {
        LOG_COMPOSITOR_ERR("GPU request queue full, dropping request with code %d\n", req.code);
    }
}

static void request_display_info(void)
{
    display_info_req_id = new_req_id();
    display_info_pending = true;
    enqueue((gpu_req_t) {
        .code = GPU_REQ_GET_DISPLAY_INFO,
        .id = display_info_req_id,
        .get_display_info = { .mem_offset = DISPLAY_INFO_OFFSET },
    });
    microkit_notify(VIRT_CH);
}

/*
 * Send rows [y, y + height) of the framebuffer to the device and flush them to
 * the scanout. We always transfer full-width bands so that the transferred
 * bytes are contiguous in the backing memory: the virtualiser bounds-checks
 * and cleans the cache for exactly width * height * bpp bytes starting at
 * mem_offset, which is only correct for a contiguous region.
 */
static void present_rows(uint32_t y, uint32_t height)
{
    gpu_rect_t band = { .x = 0, .y = y, .width = screen.width, .height = height };

    enqueue((gpu_req_t) {
        .code = GPU_REQ_TRANSFER_TO_2D,
        .id = new_req_id(),
        .transfer_to_2d = {
            .resource_id = FB_RESOURCE_ID,
            .rect = band,
            .mem_offset = (uint64_t)y * screen.stride * GPU_BPP_2D,
        },
    });
    enqueue((gpu_req_t) {
        .code = GPU_REQ_RESOURCE_FLUSH,
        .id = new_req_id(),
        .resource_flush = { .resource_id = FB_RESOURCE_ID, .rect = band },
    });
    microkit_notify(VIRT_CH);
}

/* Geometry */

static gfx_rect_t frame_rect(int slot)
{
    app_t *a = &apps[slot];
    /*
     * While a corner drag is live the frame follows the pointer, but the content
     * keeps the size the app published, so the old pixels sit in the corner of
     * the new frame until the app adopts it. No scaling: the blit stays 1:1 and
     * is clamped to what the app declared.
     */
    int32_t w = slot == resize_slot && resize_w > 0 ? resize_w : a->width;
    int32_t h = slot == resize_slot && resize_h > 0 ? resize_h : a->height;
    return (gfx_rect_t) { a->x, a->y, w + 2 * BORDER, TITLE_HEIGHT + h + BORDER };
}

static gfx_rect_t content_rect(int slot)
{
    app_t *a = &apps[slot];
    return (gfx_rect_t) { a->x + BORDER, a->y + TITLE_HEIGHT, a->width, a->height };
}

/* The corner an app that can be resized offers to drag, inside the frame's border */
static gfx_rect_t resize_handle(int slot)
{
    gfx_rect_t f = frame_rect(slot);
    return (gfx_rect_t) { f.x + f.width - RESIZE_HANDLE, f.y + f.height - RESIZE_HANDLE, RESIZE_HANDLE,
                          RESIZE_HANDLE };
}

static gfx_rect_t close_button(gfx_rect_t frame)
{
    return (gfx_rect_t) { frame.x + frame.width - CLOSE_SIZE - 8, frame.y + (TITLE_HEIGHT - CLOSE_SIZE) / 2,
                          CLOSE_SIZE, CLOSE_SIZE };
}

/* Damage tracking */

static void damage_rows(int32_t y, int32_t height)
{
    if (height <= 0) {
        return;
    }
    dirty_y0 = MIN(dirty_y0, y);
    dirty_y1 = MAX(dirty_y1, y + height);
}

/* A window's frame including its drop shadow */
static void damage_window(int slot)
{
    gfx_rect_t f = frame_rect(slot);
    damage_rows(f.y, f.height + SHADOW_OFFSET);
}

static void damage_taskbar(void)
{
    damage_rows((int32_t)screen.height - TASKBAR_HEIGHT, TASKBAR_HEIGHT);
}

static void damage_cursor(void)
{
    damage_rows(pointer_y, CURSOR_HEIGHT);
}

/* Events to apps */

/* False if the app's queue was full and the event was dropped */
static bool send_event(int slot, gui_event_t ev)
{
    if (gui_event_enqueue(apps[slot].events, GUI_EVENT_QUEUE_CAPACITY(GUI_EVENTS_REGION_SIZE), ev) == 0) {
        pending_notify |= BIT(slot);
        return true;
    }
    return false;
}

static void send_pointer_event(int slot, uint16_t type, uint16_t code, int32_t value)
{
    gfx_rect_t c = content_rect(slot);
    send_event(slot, (gui_event_t) {
        .type = type, .code = code, .value = value, .x = pointer_x - c.x, .y = pointer_y - c.y
    });
}

static void notify_apps(void)
{
    for (int slot = 0; slot < GUI_NUM_APPS; slot++) {
        if (pending_notify & BIT(slot)) {
            microkit_notify(APP_CH(slot));
        }
    }
    pending_notify = 0;
}

/* Window management */

static int topmost_visible(void)
{
    for (int i = GUI_NUM_APPS - 1; i >= 0; i--) {
        if (apps[z_order[i]].mapped && apps[z_order[i]].visible) {
            return z_order[i];
        }
    }
    return -1;
}

/* Recompute the focused window and tell the apps involved */
static void update_focus(void)
{
    int top = topmost_visible();
    if (top == focused) {
        return;
    }

    if (focused >= 0) {
        send_event(focused, (gui_event_t) { .type = GUI_EV_FOCUS, .value = 0 });
        if (apps[focused].visible) {
            damage_window(focused);
        }
    }
    if (top >= 0) {
        send_event(top, (gui_event_t) { .type = GUI_EV_FOCUS, .value = 1 });
        damage_window(top);
    }
    focused = top;
    damage_taskbar();
}

static void raise_window(int slot)
{
    int pos = 0;
    while (z_order[pos] != slot) {
        pos++;
    }
    for (; pos < GUI_NUM_APPS - 1; pos++) {
        z_order[pos] = z_order[pos + 1];
    }
    z_order[GUI_NUM_APPS - 1] = slot;
    damage_window(slot);
    update_focus();
}

static void layout_taskbar(void);

static void show_window(int slot)
{
    bool was_open = apps[slot].open;
    apps[slot].open = true;
    apps[slot].visible = true;
    raise_window(slot);
    if (!was_open) {
        layout_taskbar();
    }
}

/* Take a window off screen, keeping its tab if `keep_open` */
static void withdraw_window(int slot, bool keep_open)
{
    if (apps[slot].visible) {
        damage_window(slot);
    }
    apps[slot].visible = false;
    if (drag_slot == slot) {
        drag_slot = -1;
    }
    if (grab_slot == slot) {
        grab_slot = -1;
    }
    if (apps[slot].open && !keep_open) {
        apps[slot].open = false;
        layout_taskbar();
    }
    update_focus();
}

static void minimise_window(int slot)
{
    withdraw_window(slot, true);
}

static void close_window(int slot)
{
    withdraw_window(slot, false);
}

static void move_window(int slot, int32_t x, int32_t y)
{
    int32_t w = (int32_t)screen.width;
    int32_t h = (int32_t)screen.height;
    gfx_rect_t f = frame_rect(slot);

    /* Keep enough of the title bar on screen to grab it again */
    x = MAX(MIN(x, w - 40), 40 - f.width);
    y = MAX(MIN(y, h - TASKBAR_HEIGHT - TITLE_HEIGHT), 0);
    if (x == apps[slot].x && y == apps[slot].y) {
        return;
    }

    damage_window(slot);
    apps[slot].x = x;
    apps[slot].y = y;
    damage_window(slot);
}

/*
 * Follow a corner drag. Only the frame changes: the content keeps the size the
 * app published until it adopts this one, so nothing is read outside what it
 * declared. The drag is clamped with the same helper the app uses, so the size
 * offered is one the compositor will not later reject.
 */
static void resize_dragged(int slot)
{
    gfx_rect_t c = content_rect(slot);
    uint32_t w = pointer_x > c.x ? (uint32_t)(pointer_x - c.x) : 0;
    uint32_t h = pointer_y > c.y ? (uint32_t)(pointer_y - c.y) : 0;
    gui_size_clamp(&w, &h, GUI_SURFACE_REGION_SIZE);

    if ((int32_t)w == resize_w && (int32_t)h == resize_h) {
        return;
    }

    damage_window(slot);
    resize_w = (int32_t)w;
    resize_h = (int32_t)h;
    damage_window(slot);
}

/*
 * End a corner drag by asking the app for the size the frame is already
 * showing. If the app cannot be told, take the frame back immediately: it may
 * never commit again, so waiting for a commit that never comes would leave the
 * window showing a size it does not have.
 */
static void resize_finished(void)
{
    int slot = resize_slot;
    uint32_t w = (uint32_t)resize_w;
    uint32_t h = (uint32_t)resize_h;
    gui_size_clamp(&w, &h, GUI_SURFACE_REGION_SIZE);
    resize_w = (int32_t)w;
    resize_h = (int32_t)h;
    resizing = false;

    bool sent = send_event(slot, (gui_event_t) { .type = GUI_EV_RESIZE, .x = (int32_t)w, .y = (int32_t)h });
    if (!sent) {
        LOG_COMPOSITOR("app %d's queue was full, dropping a resize to %ux%u\n", slot, w, h);
        damage_window(slot);
        resize_w = 0;
        resize_h = 0;
        resize_slot = -1;
        damage_window(slot);
    }
}

/* Initial position of a newly mapped window, by slot */
static void place_window(int slot)
{
    int32_t w = (int32_t)screen.width;
    int32_t h = (int32_t)screen.height;
    gfx_rect_t f = frame_rect(slot);

    switch (slot) {
    case 0:
        apps[slot].x = 40;
        apps[slot].y = 40;
        break;
    case 1:
        apps[slot].x = w - f.width - 40;
        apps[slot].y = 70;
        break;
    case 2:
        apps[slot].x = 60;
        /* Above the wallpaper logo */
        apps[slot].y = h - TASKBAR_HEIGHT - f.height - 110;
        break;
    case 3:
        apps[slot].x = (w - f.width) / 2 + 60;
        apps[slot].y = 50;
        break;
    case 4:
        apps[slot].x = (w - f.width) / 2 - 60;
        apps[slot].y = 90;
        break;
    case 5:
        /* Files, to the side of the list of apps */
        apps[slot].x = (w - f.width) / 2 - 80;
        apps[slot].y = (h - TASKBAR_HEIGHT - f.height) / 2;
        break;
    case 6:
        apps[slot].x = (w - f.width) / 2;
        apps[slot].y = (h - TASKBAR_HEIGHT - f.height) / 2;
        break;
    default:
        /* Windows of WebAssembly apps go in the corners, around the list at the centre */
        apps[slot].x = (slot - GUI_FIRST_WASM_WINDOW) % 2 ? w - f.width - 20 : 20;
        apps[slot].y = (slot - GUI_FIRST_WASM_WINDOW) / 2 % 2 ? h - TASKBAR_HEIGHT - f.height - 20 : 20;
        break;
    }
    apps[slot].x = MAX(MIN(apps[slot].x, w - f.width), 0);
    apps[slot].y = MAX(MIN(apps[slot].y, h - TASKBAR_HEIGHT - f.height), 0);
}

static void layout_taskbar(void)
{
    int32_t bar_y = (int32_t)screen.height - TASKBAR_HEIGHT;
    int32_t tab_x = 6 + gfx_text_width("Lions", TEXT_SCALE) + 20 + 12;
    for (int slot = 0; slot < GUI_NUM_APPS; slot++) {
        if (!apps[slot].mapped || !apps[slot].open) {
            apps[slot].tab = (gfx_rect_t) { 0, 0, 0, 0 };
            continue;
        }
        int32_t tab_w = gfx_text_width(apps[slot].title, TEXT_SCALE) + 16;
        apps[slot].tab = (gfx_rect_t) { tab_x, bar_y + 5, tab_w, TASKBAR_HEIGHT - 10 };
        tab_x += tab_w + 6;
    }
    damage_taskbar();
}

/*
 * Pick up a commit from an app. Everything in its state page is untrusted and
 * may change while we read it, so copy, validate, and fall back to redrawing
 * the whole window when anything looks inconsistent.
 */
static void app_committed(int slot)
{
    app_t *a = &apps[slot];
    uint32_t seq = __atomic_load_n(&a->state->seq, __ATOMIC_ACQUIRE);
    gui_rect_t damage = a->state->damage;
    uint32_t magic = a->state->magic;
    int32_t width = (int32_t)a->state->width;
    int32_t height = (int32_t)a->state->height;
    char title[GUI_TITLE_MAX];
    for (int i = 0; i < GUI_TITLE_MAX - 1; i++) {
        char c = a->state->title[i];
        title[i] = (c >= ' ' && c <= '~') || c == '\0' ? c : '?';
    }
    title[GUI_TITLE_MAX - 1] = '\0';
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    bool consistent = __atomic_load_n(&a->state->seq, __ATOMIC_ACQUIRE) == seq && seq == a->seen_seq + 1;
    a->seen_seq = seq;

    if (magic != GUI_STATE_MAGIC || width <= 0 || height <= 0 || width > APP_MAX_DIMENSION
        || height > APP_MAX_DIMENSION || (uint64_t)width * height * sizeof(uint32_t) > GUI_SURFACE_REGION_SIZE) {
        if (a->mapped) {
            if (magic == 0) {
                LOG_COMPOSITOR("app %d withdrew its window\n", slot);
            } else {
                LOG_COMPOSITOR_ERR("app %d published an invalid surface, closing it\n", slot);
            }
            close_window(slot);
            a->mapped = false;
            layout_taskbar();
        }
        return;
    }

    bool title_changed = false;
    for (int i = 0; i < GUI_TITLE_MAX; i++) {
        if (title[i] != a->title[i]) {
            title_changed = true;
        }
        a->title[i] = title[i];
        if (title[i] == '\0') {
            break;
        }
    }

    /*
     * What the app says it supports. Read from the same untrusted page, but a
     * flag can only ever affect this app's own window.
     */
    a->resizable = (a->state->flags & GUI_FLAG_RESIZABLE) != 0;

    /*
     * Any commit at all means the app has had its say, so stop overriding the
     * frame and show the size it really has. This is the backstop that recovers
     * if the resize event was dropped: the window cannot stay letterboxed.
     */
    if (slot == resize_slot) {
        resize_w = 0;
        resize_h = 0;
        resize_slot = -1;
    }

    if (!a->mapped) {
        a->width = width;
        a->height = height;
        a->mapped = true;
        place_window(slot);
        LOG_COMPOSITOR("app %d '%s' mapped a %dx%d window\n", slot, a->title, width, height);
        if ((GUI_START_OPEN | GUI_OPEN_ON_MAP) & BIT(slot)) {
            show_window(slot);
        }
        return;
    }

    if (width != a->width || height != a->height) {
        damage_window(slot);
        a->width = width;
        a->height = height;
        consistent = false;
    }
    if (title_changed) {
        layout_taskbar();
        consistent = false;
    }
    if (!a->visible) {
        return;
    }

    if (!consistent) {
        damage_window(slot);
        return;
    }

    /* Clamp the damage to the content before trusting it */
    int32_t y0 = MAX(damage.y, 0);
    int32_t y1 = MIN((int64_t)damage.y + damage.height, height);
    if (y1 > y0 && damage.width > 0) {
        damage_rows(content_rect(slot).y + y0, y1 - y0);
    }
}

/* Drawing */

static void draw_window(int slot, bool is_focused)
{
    app_t *a = &apps[slot];
    gfx_rect_t frame = frame_rect(slot);
    gfx_rect_t content = content_rect(slot);

    gfx_fill_rect(&screen, (gfx_rect_t) { frame.x + SHADOW_OFFSET, frame.y + SHADOW_OFFSET, frame.width, frame.height },
                  COLOUR_SHADOW);
    gfx_fill_rect(&screen, frame, COLOUR_WINDOW_BORDER);
    gfx_fill_rect(&screen, (gfx_rect_t) { frame.x, frame.y, frame.width, TITLE_HEIGHT },
                  is_focused ? COLOUR_TITLE_ACTIVE : COLOUR_TITLE_INACTIVE);

    /* Keep the title clear of the close button */
    gfx_rect_t title_clip = { frame.x, frame.y, frame.width - CLOSE_SIZE - 16, TITLE_HEIGHT };
    gfx_rect_t saved = screen.clip;
    gfx_rect_t clip = saved;
    int32_t cx0 = MAX(clip.x, title_clip.x), cx1 = MIN(clip.x + clip.width, title_clip.x + title_clip.width);
    int32_t cy0 = MAX(clip.y, title_clip.y), cy1 = MIN(clip.y + clip.height, title_clip.y + title_clip.height);
    if (cx1 > cx0 && cy1 > cy0) {
        screen.clip = (gfx_rect_t) { cx0, cy0, cx1 - cx0, cy1 - cy0 };
        gfx_draw_text(&screen, frame.x + 8, frame.y + (TITLE_HEIGHT - CHAR_HEIGHT) / 2, a->title, TEXT_SCALE,
                      COLOUR_TEXT_LIGHT);
        screen.clip = saved;
    }
    gfx_fill_rect(&screen, close_button(frame), COLOUR_CLOSE);

    gfx_blit(&screen, content.x, content.y, a->pixels, a->width, a->height, a->width);
}

static void draw_taskbar(void)
{
    int32_t w = (int32_t)screen.width;
    int32_t bar_y = (int32_t)screen.height - TASKBAR_HEIGHT;
    int32_t text_y = bar_y + (TASKBAR_HEIGHT - CHAR_HEIGHT) / 2;

    gfx_fill_rect(&screen, (gfx_rect_t) { 0, bar_y, w, TASKBAR_HEIGHT }, COLOUR_TASKBAR);
    gfx_fill_rect(&screen, (gfx_rect_t) { 0, bar_y, w, 1 }, COLOUR_TASKBAR_EDGE);

    gfx_rect_t start = { 6, bar_y + 5, gfx_text_width("Lions", TEXT_SCALE) + 20, TASKBAR_HEIGHT - 10 };
    gfx_fill_rect(&screen, start, menu_open ? COLOUR_TEXT_LIGHT : COLOUR_BUTTON);
    gfx_draw_text(&screen, start.x + 10, text_y, "Lions", TEXT_SCALE, COLOUR_TEXT_DARK);

    int32_t tabs_end = start.x + start.width;
    for (int slot = 0; slot < GUI_NUM_APPS; slot++) {
        app_t *a = &apps[slot];
        if (!a->mapped || !a->open) {
            continue;
        }
        tabs_end = a->tab.x + a->tab.width;
        if (slot == focused) {
            gfx_fill_rect(&screen, a->tab, COLOUR_TAB_ACTIVE);
        }
        gfx_draw_text(&screen, a->tab.x + 8, text_y, a->title, TEXT_SCALE,
                      a->visible ? COLOUR_TEXT_TAB : COLOUR_TEXT_TAB_HIDDEN);
    }

    /* Only when there is room next to the tabs */
    char label[40];
    if (GUI_WASM_WINDOWS) {
        sddf_snprintf(label, sizeof(label), "%d app PDs + %d sandboxes", GUI_NUM_FIXED_APPS, GUI_WASM_WINDOWS);
    } else {
        sddf_snprintf(label, sizeof(label), "compositor + %d app PDs", GUI_NUM_FIXED_APPS);
    }
    int32_t label_x = w - gfx_text_width(label, TEXT_SCALE) - 12;
    if (label_x > tabs_end + 24) {
        gfx_draw_text(&screen, label_x, text_y, label, TEXT_SCALE, COLOUR_TEXT_TAB_HIDDEN);
    }
}

static void draw_cursor(void)
{
    for (int32_t y = 0; y < CURSOR_HEIGHT; y++) {
        for (int32_t x = 0; x < CURSOR_WIDTH; x++) {
            char c = cursor_image[y][x];
            if (c == 'X' || c == '.') {
                gfx_fill_rect(&screen, (gfx_rect_t) { pointer_x + x, pointer_y + y, 1, 1 },
                              c == 'X' ? COLOUR_CURSOR_EDGE : COLOUR_CURSOR_FILL);
            }
        }
    }
}

/* Launcher menu: one item per mapped app, in slot order, above the Lions button */

static gfx_rect_t lions_button(void)
{
    return (gfx_rect_t) { 6, (int32_t)screen.height - TASKBAR_HEIGHT + 5, gfx_text_width("Lions", TEXT_SCALE) + 20,
                          TASKBAR_HEIGHT - 10 };
}

static int menu_items(int *slots)
{
    int n = 0;
    for (int slot = 0; slot < GUI_NUM_APPS; slot++) {
        if (apps[slot].mapped) {
            slots[n++] = slot;
        }
    }
    return n;
}

static gfx_rect_t menu_rect(void)
{
    int slots[GUI_NUM_APPS];
    int n = menu_items(slots);
    int32_t w = gfx_text_width("Applications", TEXT_SCALE);
    for (int i = 0; i < n; i++) {
        w = MAX(w, gfx_text_width(apps[slots[i]].title, TEXT_SCALE));
    }
    w += 2 * MENU_PADDING + 24;
    int32_t h = (n + 1) * MENU_ITEM_HEIGHT + 2 * MENU_PADDING;
    return (gfx_rect_t) { 6, (int32_t)screen.height - TASKBAR_HEIGHT - h - 4, w, h };
}

static gfx_rect_t menu_item_rect(int index)
{
    gfx_rect_t m = menu_rect();
    return (gfx_rect_t) { m.x + MENU_PADDING, m.y + MENU_PADDING + (index + 1) * MENU_ITEM_HEIGHT,
                          m.width - 2 * MENU_PADDING, MENU_ITEM_HEIGHT };
}

static void damage_menu(void)
{
    gfx_rect_t m = menu_rect();
    damage_rows(m.y, m.height + SHADOW_OFFSET);
}

static void set_menu_open(bool open)
{
    if (open == menu_open) {
        return;
    }
    damage_menu();
    damage_taskbar();
    menu_open = open;
    menu_hover = -1;
}

static int menu_item_at_pointer(void)
{
    int slots[GUI_NUM_APPS];
    int n = menu_items(slots);
    for (int i = 0; i < n; i++) {
        if (gfx_rect_contains(menu_item_rect(i), pointer_x, pointer_y)) {
            return i;
        }
    }
    return -1;
}

static void draw_menu(void)
{
    int slots[GUI_NUM_APPS];
    int n = menu_items(slots);
    gfx_rect_t m = menu_rect();

    gfx_fill_rect(&screen, (gfx_rect_t) { m.x + SHADOW_OFFSET, m.y + SHADOW_OFFSET, m.width, m.height },
                  COLOUR_SHADOW);
    gfx_fill_rect(&screen, m, COLOUR_MENU);
    gfx_draw_rect(&screen, m, 1, COLOUR_WINDOW_BORDER);
    gfx_draw_text(&screen, m.x + MENU_PADDING + 8, m.y + MENU_PADDING + (MENU_ITEM_HEIGHT - CHAR_HEIGHT) / 2,
                  "Applications", TEXT_SCALE, COLOUR_MENU_HEADER);

    for (int i = 0; i < n; i++) {
        app_t *a = &apps[slots[i]];
        gfx_rect_t item = menu_item_rect(i);
        if (i == menu_hover) {
            gfx_fill_rect(&screen, item, COLOUR_MENU_HOVER);
        }
        /* A dot marks apps whose window is open */
        if (a->open) {
            gfx_fill_rect(&screen, (gfx_rect_t) { item.x + 8, item.y + MENU_ITEM_HEIGHT / 2 - 2, 4, 4 },
                          COLOUR_TEXT_LIGHT);
        }
        gfx_draw_text(&screen, item.x + 20, item.y + (MENU_ITEM_HEIGHT - CHAR_HEIGHT) / 2, a->title, TEXT_SCALE,
                      COLOUR_TEXT_LIGHT);
    }
}

/* Redraw the whole scene, limited to rows [y0, y1) */
static void render(int32_t y0, int32_t y1)
{
    int32_t w = (int32_t)screen.width;
    int32_t h = (int32_t)screen.height;
    gfx_set_clip(&screen, (gfx_rect_t) { 0, y0, w, y1 - y0 });

    gfx_fill_vgradient(&screen, (gfx_rect_t) { 0, 0, w, h - TASKBAR_HEIGHT }, COLOUR_BG_TOP, COLOUR_BG_BOTTOM);
    gfx_draw_text(&screen, 24, h - TASKBAR_HEIGHT - 64, "LionsOS", 4, COLOUR_BG_LOGO);
    gfx_draw_text(&screen, 24, h - TASKBAR_HEIGHT - 26, "Lions opens apps, Alt+Tab switches windows",
                  TEXT_SCALE, COLOUR_BG_HINT);

    draw_taskbar();
    for (int i = 0; i < GUI_NUM_APPS; i++) {
        int slot = z_order[i];
        if (apps[slot].mapped && apps[slot].visible) {
            draw_window(slot, slot == focused);
        }
    }
    if (menu_open) {
        draw_menu();
    }
    draw_cursor();

    gfx_reset_clip(&screen);
}

/* Redraw and present everything that has been damaged since the last flush */
static void flush(void)
{
    notify_apps();

    if (!running || dirty_y1 <= dirty_y0) {
        return;
    }

    int32_t y0 = MAX(dirty_y0, 0);
    int32_t y1 = MIN(dirty_y1, (int32_t)screen.height);
    dirty_y0 = INT32_MAX;
    dirty_y1 = 0;
    if (y1 <= y0) {
        return;
    }

    render(y0, y1);
    present_rows(y0, y1 - y0);
}

/* Input */

/* The window whose frame is under the pointer, or -1 */
static int window_at_pointer(void)
{
    for (int i = GUI_NUM_APPS - 1; i >= 0; i--) {
        int slot = z_order[i];
        if (apps[slot].mapped && apps[slot].visible
            && gfx_rect_contains(frame_rect(slot), pointer_x, pointer_y)) {
            return slot;
        }
    }
    return -1;
}

static void pointer_moved(int32_t x, int32_t y)
{
    x = MAX(MIN(x, (int32_t)screen.width - 1), 0);
    y = MAX(MIN(y, (int32_t)screen.height - 1), 0);
    if (x == pointer_x && y == pointer_y) {
        return;
    }

    damage_cursor();
    pointer_x = x;
    pointer_y = y;
    damage_cursor();

    if (menu_open) {
        int hover = menu_item_at_pointer();
        if (hover != menu_hover) {
            menu_hover = hover;
            damage_menu();
        }
        return;
    }

    if (drag_slot >= 0) {
        move_window(drag_slot, pointer_x - drag_dx, pointer_y - drag_dy);
    } else if (resizing) {
        resize_dragged(resize_slot);
    } else if (grab_slot >= 0) {
        send_pointer_event(grab_slot, GUI_EV_POINTER_MOTION, 0, 0);
    } else if (focused >= 0 && window_at_pointer() == focused
               && gfx_rect_contains(content_rect(focused), pointer_x, pointer_y)) {
        send_pointer_event(focused, GUI_EV_POINTER_MOTION, 0, 0);
    }
}

static void pointer_pressed(uint16_t button)
{
    if (gfx_rect_contains(lions_button(), pointer_x, pointer_y)) {
        if (button == INPUT_BTN_LEFT) {
            set_menu_open(!menu_open);
        }
        return;
    }

    /* While the menu is open, a click either launches an app or dismisses it */
    if (menu_open) {
        int item = menu_item_at_pointer();
        set_menu_open(false);
        if (item >= 0 && button == INPUT_BTN_LEFT) {
            int slots[GUI_NUM_APPS];
            menu_items(slots);
            show_window(slots[item]);
        }
        return;
    }

    for (int slot = 0; slot < GUI_NUM_APPS; slot++) {
        if (apps[slot].open && gfx_rect_contains(apps[slot].tab, pointer_x, pointer_y)) {
            if (button != INPUT_BTN_LEFT) {
                return;
            }
            if (apps[slot].visible && focused == slot) {
                minimise_window(slot);
            } else {
                show_window(slot);
            }
            return;
        }
    }

    int slot = window_at_pointer();
    if (slot < 0) {
        return;
    }
    gfx_rect_t frame = frame_rect(slot);

    if (focused != slot) {
        raise_window(slot);
    }

    if (gfx_rect_contains(content_rect(slot), pointer_x, pointer_y)) {
        grab_slot = slot;
        send_pointer_event(slot, GUI_EV_POINTER_BUTTON, button, INPUT_KEY_PRESSED);
        return;
    }

    if (button != INPUT_BTN_LEFT) {
        return;
    }
    if (gfx_rect_contains(close_button(frame), pointer_x, pointer_y)) {
        send_event(slot, (gui_event_t) { .type = GUI_EV_CLOSE });
        close_window(slot);
    } else if (apps[slot].resizable && gfx_rect_contains(resize_handle(slot), pointer_x, pointer_y)) {
        resize_slot = slot;
        resize_w = apps[slot].width;
        resize_h = apps[slot].height;
        resizing = true;
    } else if (pointer_y < frame.y + TITLE_HEIGHT) {
        drag_slot = slot;
        drag_dx = pointer_x - frame.x;
        drag_dy = pointer_y - frame.y;
    }
}

static void pointer_released(uint16_t button)
{
    if (grab_slot >= 0) {
        send_pointer_event(grab_slot, GUI_EV_POINTER_BUTTON, button, INPUT_KEY_RELEASED);
        grab_slot = -1;
    }
    drag_slot = -1;
    if (resizing) {
        resize_finished();
    }
}

/*
 * Axis events of one frame describe a single movement, so collect them and
 * move the pointer once, at the end of the frame or before a button event.
 */
static void queue_motion(int32_t x, int32_t y)
{
    if (!motion_pending) {
        next_pointer_x = pointer_x;
        next_pointer_y = pointer_y;
        motion_pending = true;
    }
    if (x != INT32_MIN) {
        next_pointer_x = x;
    }
    if (y != INT32_MIN) {
        next_pointer_y = y;
    }
}

static void apply_motion(void)
{
    if (motion_pending) {
        motion_pending = false;
        pointer_moved(next_pointer_x, next_pointer_y);
    }
}

/* Bring the backmost open window to the front, so repeated use cycles through all of them */
static void cycle_windows(void)
{
    for (int i = 0; i < GUI_NUM_APPS; i++) {
        int slot = z_order[i];
        if (apps[slot].mapped && apps[slot].open && slot != focused) {
            show_window(slot);
            return;
        }
    }
}

/* Keyboard shortcuts of the shell. Returns true if the compositor consumed the key. */
static bool handle_shortcut(input_event_t *ev)
{
    bool down = ev->value != INPUT_KEY_RELEASED;
    switch (ev->code) {
    case INPUT_KEY_LEFTALT:
        alt_left = down;
        return false;
    case INPUT_KEY_RIGHTALT:
        alt_right = down;
        return false;
    case INPUT_KEY_TAB:
        if (!alt_left && !alt_right) {
            return false;
        }
        if (down) {
            set_menu_open(false);
            cycle_windows();
        }
        return true;
    case INPUT_KEY_ESC:
        if (!menu_open) {
            return false;
        }
        if (down) {
            set_menu_open(false);
        }
        return true;
    default:
        return false;
    }
}

static void handle_input_event(input_event_t *ev)
{
    int32_t base_x = motion_pending ? next_pointer_x : pointer_x;
    int32_t base_y = motion_pending ? next_pointer_y : pointer_y;

    switch (ev->type) {
    case INPUT_EV_SYN:
        if (ev->code == INPUT_SYN_REPORT) {
            apply_motion();
        }
        break;
    case INPUT_EV_ABS:
        if (ev->code == INPUT_ABS_X) {
            queue_motion(((int64_t)ev->value * (screen.width - 1)) / INPUT_ABS_MAX, INT32_MIN);
        } else if (ev->code == INPUT_ABS_Y) {
            queue_motion(INT32_MIN, ((int64_t)ev->value * (screen.height - 1)) / INPUT_ABS_MAX);
        }
        break;
    case INPUT_EV_REL:
        if (ev->code == INPUT_REL_X) {
            queue_motion(base_x + ev->value, INT32_MIN);
        } else if (ev->code == INPUT_REL_Y) {
            queue_motion(INT32_MIN, base_y + ev->value);
        }
        break;
    case INPUT_EV_KEY:
        if (ev->code >= INPUT_BTN_LEFT && ev->code <= INPUT_BTN_MIDDLE) {
            apply_motion();
            if (ev->value == INPUT_KEY_PRESSED) {
                pointer_pressed(ev->code);
            } else if (ev->value == INPUT_KEY_RELEASED) {
                pointer_released(ev->code);
            }
        } else if (ev->code == INPUT_BTN_TOUCH) {
            /* Touch devices also report BTN_LEFT, so ignore the duplicate */
        } else if (handle_shortcut(ev)) {
            /* Consumed by the shell */
        } else if (focused >= 0) {
            send_event(focused, (gui_event_t) { .type = GUI_EV_KEY, .code = ev->code, .value = ev->value });
        }
        break;
    default:
        break;
    }
}

static void handle_input(void)
{
    input_event_t ev;
    while (input_dequeue(&input_handle, &ev) == 0) {
        if (running) {
            handle_input_event(&ev);
        }
    }
    /* A frame may continue in the next batch; show where the pointer is now */
    apply_motion();
    flush();
}

/* Setup */

static bool setup_display(gpu_resp_get_display_info_t *info)
{
    if (info->num_scanouts == 0 || !info->scanouts[SCANOUT_ID].enabled) {
        LOG_COMPOSITOR_ERR("scanout %d is not available\n", SCANOUT_ID);
        return false;
    }

    scanout_rect = info->scanouts[SCANOUT_ID].rect;
    uint64_t bytes = (uint64_t)scanout_rect.width * scanout_rect.height * GPU_BPP_2D;
    if (bytes > FB_MAX_BYTES) {
        LOG_COMPOSITOR_ERR("scanout %ux%u needs %lu bytes but only %lu are available, "
                           "use a smaller mode or grow GPU_DATA_REGION_SIZE_CLI0\n",
                           scanout_rect.width, scanout_rect.height, (unsigned long)bytes,
                           (unsigned long)FB_MAX_BYTES);
        return false;
    }

    screen = (gfx_surface_t) {
        .pixels = (uint32_t *)(gpu_data + FB_OFFSET),
        .width = scanout_rect.width,
        .height = scanout_rect.height,
        .stride = scanout_rect.width,
    };
    gfx_reset_clip(&screen);
    LOG_COMPOSITOR("scanout %d is %ux%u\n", SCANOUT_ID, screen.width, screen.height);

    pointer_x = screen.width / 2;
    pointer_y = screen.height / 2;
    running = true;

    /* Pick up whatever the apps committed before the display was ready */
    for (int slot = 0; slot < GUI_NUM_APPS; slot++) {
        apps[slot].seen_seq = __atomic_load_n(&apps[slot].state->seq, __ATOMIC_ACQUIRE) - 1;
        app_committed(slot);
    }
    layout_taskbar();

    render(0, screen.height);
    dirty_y0 = INT32_MAX;
    dirty_y1 = 0;

    gpu_rect_t full = { .x = 0, .y = 0, .width = screen.width, .height = screen.height };
    enqueue((gpu_req_t) {
        .code = GPU_REQ_RESOURCE_CREATE_2D,
        .id = new_req_id(),
        .resource_create_2d = {
            .resource_id = FB_RESOURCE_ID,
            .width = screen.width,
            .height = screen.height,
            .format = GPU_FORMAT_B8G8R8A8_UNORM,
        },
    });
    enqueue((gpu_req_t) {
        .code = GPU_REQ_RESOURCE_ATTACH_BACKING,
        .id = new_req_id(),
        .resource_attach_backing = {
            .resource_id = FB_RESOURCE_ID,
            .mem_offset = FB_OFFSET,
            .mem_size = bytes,
        },
    });
    enqueue((gpu_req_t) {
        .code = GPU_REQ_SET_SCANOUT,
        .id = new_req_id(),
        .set_scanout = { .resource_id = FB_RESOURCE_ID, .scanout_id = SCANOUT_ID, .rect = full },
    });
    present_rows(0, screen.height);
    notify_apps();

    return true;
}

static void handle_gpu_responses(void)
{
    gpu_resp_t resp;
    while (!gpu_dequeue_resp(&gpu_queue_handle, &resp)) {
        bool is_display_info = resp.id == display_info_req_id;
        if (is_display_info) {
            display_info_pending = false;
        }
        if (resp.status != GPU_RESP_OK) {
            LOG_COMPOSITOR_ERR("request %u failed with status %d\n", resp.id, resp.status);
            continue;
        }
        if (!is_display_info) {
            continue;
        }

        gpu_resp_get_display_info_t info;
        memcpy(&info, (void *)(gpu_data + DISPLAY_INFO_OFFSET), sizeof(info));

        if (!running) {
            setup_display(&info);
        } else if (info.scanouts[SCANOUT_ID].rect.width != scanout_rect.width
                   || info.scanouts[SCANOUT_ID].rect.height != scanout_rect.height) {
            /* TODO: recreate the resource at the new size */
            LOG_COMPOSITOR("display is now %ux%u, resizing is not supported yet\n",
                           info.scanouts[SCANOUT_ID].rect.width, info.scanouts[SCANOUT_ID].rect.height);
        }
    }
}

void init(void)
{
    LOG_COMPOSITOR("starting with %d app slots\n", GUI_NUM_APPS);
    gpu_queue_init(&gpu_queue_handle, gpu_req_queue, gpu_resp_queue, GPU_QUEUE_CAPACITY_CLI0);
    input_queue_init(&input_handle, (input_queue_t *)input_queue, INPUT_QUEUE_CAPACITY(INPUT_QUEUE_REGION_SIZE));

    for (int slot = 0; slot < GUI_NUM_APPS; slot++) {
        apps[slot].state = (const volatile gui_state_t *)(gui_states + slot * GUI_STATE_REGION_SIZE);
        apps[slot].pixels = (const uint32_t *)(gui_surfaces + slot * GUI_SURFACE_REGION_SIZE);
        apps[slot].events = (gui_event_queue_t *)(gui_events + slot * GUI_EVENTS_REGION_SIZE);
        z_order[slot] = slot;
    }

    request_display_info();
}

void notified(microkit_channel ch)
{
    if (ch == VIRT_CH) {
        handle_gpu_responses();
        if (gpu_events_check_display_info(gpu_events) && !display_info_pending) {
            gpu_events_clear_display_info(gpu_events);
            request_display_info();
        }
    } else if (ch == INPUT_CH) {
        handle_input();
    } else if (ch >= APP_CH(0) && ch < APP_CH(GUI_NUM_APPS)) {
        if (running) {
            app_committed(ch - APP_CH(0));
            flush();
        }
    } else {
        LOG_COMPOSITOR_ERR("notification on unexpected channel %u\n", ch);
    }
}
