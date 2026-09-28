/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The LionsOS desktop: a native client of the sDDF GPU virtualiser and the
 * LionsOS input virtualiser.
 *
 * The desktop keeps a small scene (wallpaper, taskbar, windows in z-order and
 * a software cursor on top) and software-renders it into its GPU data region,
 * which is scanned out as a 2D resource. Every change to the scene marks the
 * rows it touches as damaged; once all pending input or timer work has been
 * handled, only the damaged band is redrawn and sent to the device.
 *
 * Nothing here involves a driver VM:
 *   desktop -> gpu_virt   -> virtio-gpu driver
 *   desktop <- input_virt <- virtio-input drivers (keyboard, tablet)
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
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <lions/input/input.h>
#include <gpu_config.h>
#include <input_config.h>
#include "gfx.h"
#include "keymap.h"

#define LOG_DESKTOP(...) do{ sddf_dprintf("DESKTOP|INFO: "); sddf_dprintf(__VA_ARGS__); }while(0)
#define LOG_DESKTOP_ERR(...) do{ sddf_dprintf("DESKTOP|ERROR: "); sddf_dprintf(__VA_ARGS__); }while(0)

/* Fixed by meta.py, the timer channel comes from the generated config */
#define VIRT_CH 0
#define INPUT_CH 2

#define DISPLAY_INFO_OFFSET 0
#define FB_OFFSET 0x1000
#define FB_MAX_BYTES (GPU_DATA_REGION_SIZE_CLI0 - FB_OFFSET)

#define SCANOUT_ID 0
#define FB_RESOURCE_ID 1

#define CLOCK_TICK_NS NS_IN_S

/* Theme */
#define COLOUR_BG_TOP GFX_RGB(0x14, 0x2a, 0x4f)
#define COLOUR_BG_BOTTOM GFX_RGB(0x2e, 0x7d, 0x8c)
#define COLOUR_BG_LOGO GFX_RGB(0x9f, 0xc4, 0xd1)
#define COLOUR_TASKBAR GFX_RGB(0x1b, 0x1f, 0x27)
#define COLOUR_TASKBAR_EDGE GFX_RGB(0x3a, 0x41, 0x4f)
#define COLOUR_TAB_ACTIVE GFX_RGB(0x33, 0x3a, 0x48)
#define COLOUR_BUTTON GFX_RGB(0xe0, 0x8a, 0x1e)
#define COLOUR_TEXT_LIGHT GFX_RGB(0xf2, 0xf2, 0xf2)
#define COLOUR_TEXT_TAB GFX_RGB(0xb8, 0xbe, 0xc9)
#define COLOUR_TEXT_TAB_HIDDEN GFX_RGB(0x6b, 0x72, 0x80)
#define COLOUR_TEXT_DARK GFX_RGB(0x22, 0x22, 0x22)
#define COLOUR_TEXT_MUTED GFX_RGB(0x8a, 0x8f, 0x99)
#define COLOUR_WINDOW_BODY GFX_RGB(0xf7, 0xf5, 0xf0)
#define COLOUR_WINDOW_BORDER GFX_RGB(0x0f, 0x14, 0x1c)
#define COLOUR_TITLE_ACTIVE GFX_RGB(0x2f, 0x5d, 0xa8)
#define COLOUR_TITLE_INACTIVE GFX_RGB(0x6b, 0x72, 0x80)
#define COLOUR_SHADOW GFX_RGB(0x0a, 0x12, 0x20)
#define COLOUR_CLOSE GFX_RGB(0xd9, 0x4c, 0x3d)
#define COLOUR_CURSOR_EDGE GFX_RGB(0x00, 0x00, 0x00)
#define COLOUR_CURSOR_FILL GFX_RGB(0xff, 0xff, 0xff)

#define TASKBAR_HEIGHT 36
#define TITLE_HEIGHT 24
#define SHADOW_OFFSET 6
#define TEXT_SCALE 2
#define CHAR_WIDTH (GFX_FONT_WIDTH * TEXT_SCALE)
#define CHAR_HEIGHT (GFX_FONT_HEIGHT * TEXT_SCALE)
#define LINE_HEIGHT (CHAR_HEIGHT + 6)
#define TEXT_PADDING 12
#define CLOSE_SIZE 12

#define NOTES_MAX 512

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

gpu_events_t *gpu_events;
gpu_req_queue_t *gpu_req_queue;
gpu_resp_queue_t *gpu_resp_queue;
uintptr_t gpu_data;
uintptr_t input_queue;

static sddf_channel timer_ch;
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

/* Scene */
enum window_id {
    WIN_WELCOME,
    WIN_DISPLAY,
    WIN_NOTES,
    NUM_WINDOWS,
};

typedef struct window {
    const char *title;
    const char *tab_label;
    gfx_rect_t frame;
    bool visible;
} window_t;

static window_t windows[NUM_WINDOWS];
/* Window ids from back to front */
static int z_order[NUM_WINDOWS] = { WIN_WELCOME, WIN_DISPLAY, WIN_NOTES };
static gfx_rect_t tab_rects[NUM_WINDOWS];
static gfx_rect_t clock_rect;

static char notes[NOTES_MAX + 1];
static uint32_t notes_len = 0;

/* Pointer and keyboard state */
static int32_t pointer_x;
static int32_t pointer_y;
static bool shift_left, shift_right;
static int drag_window = -1;
static int32_t drag_dx, drag_dy;

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
        LOG_DESKTOP_ERR("GPU request queue full, dropping request with code %d\n", req.code);
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

/* Damage tracking */

static void damage_rows(int32_t y, int32_t height)
{
    if (height <= 0) {
        return;
    }
    dirty_y0 = MIN(dirty_y0, y);
    dirty_y1 = MAX(dirty_y1, y + height);
}

static void damage_rect(gfx_rect_t r)
{
    damage_rows(r.y, r.height);
}

/* A window's frame including its drop shadow */
static void damage_window(int id)
{
    gfx_rect_t f = windows[id].frame;
    damage_rows(f.y, f.height + SHADOW_OFFSET);
}

static void damage_cursor(void)
{
    damage_rows(pointer_y, CURSOR_HEIGHT);
}

/* Drawing */

static gfx_rect_t close_button(gfx_rect_t frame)
{
    return (gfx_rect_t) { frame.x + frame.width - CLOSE_SIZE - 8, frame.y + (TITLE_HEIGHT - CLOSE_SIZE) / 2,
                          CLOSE_SIZE, CLOSE_SIZE };
}

static int focused_window(void)
{
    for (int i = NUM_WINDOWS - 1; i >= 0; i--) {
        if (windows[z_order[i]].visible) {
            return z_order[i];
        }
    }
    return -1;
}

static int32_t draw_lines(gfx_rect_t frame, const char *const *lines, uint32_t num_lines)
{
    int32_t y = frame.y + TITLE_HEIGHT + TEXT_PADDING;
    for (uint32_t i = 0; i < num_lines; i++) {
        uint32_t colour = lines[i][0] == ' ' ? COLOUR_TEXT_MUTED : COLOUR_TEXT_DARK;
        gfx_draw_text(&screen, frame.x + TEXT_PADDING, y, lines[i], TEXT_SCALE, colour);
        y += LINE_HEIGHT;
    }
    return y;
}

static void draw_welcome(gfx_rect_t frame)
{
    static const char *const lines[] = {
        "A native desktop on seL4.",
        "",
        "Drag windows by their title,",
        "close them with the red box",
        "and reopen them from the",
        "taskbar. Click Notes to type.",
        "",
        " No Linux, no driver VM.",
    };
    draw_lines(frame, lines, ARRAY_SIZE(lines));
}

static void draw_display(gfx_rect_t frame)
{
    char mode[32], pointer[32];
    sddf_snprintf(mode, sizeof(mode), "Mode:    %ux%u", screen.width, screen.height);
    sddf_snprintf(pointer, sizeof(pointer), "Pointer: %d,%d", pointer_x, pointer_y);
    const char *const lines[] = { mode, "Format:  B8G8R8A8", pointer };
    draw_lines(frame, lines, ARRAY_SIZE(lines));
}

/* Rows of the Display window whose content follows the pointer */
static void damage_pointer_readout(void)
{
    if (windows[WIN_DISPLAY].visible) {
        damage_rows(windows[WIN_DISPLAY].frame.y + TITLE_HEIGHT + TEXT_PADDING + 2 * LINE_HEIGHT, CHAR_HEIGHT);
    }
}

static void draw_notes(gfx_rect_t frame, bool focused)
{
    int32_t cols = (frame.width - 2 * TEXT_PADDING) / CHAR_WIDTH;
    int32_t rows = (frame.height - TITLE_HEIGHT - 2 * TEXT_PADDING) / LINE_HEIGHT;
    if (cols <= 0 || rows <= 0) {
        return;
    }

    if (notes_len == 0 && !focused) {
        gfx_draw_text(&screen, frame.x + TEXT_PADDING, frame.y + TITLE_HEIGHT + TEXT_PADDING, "Click to type...",
                      TEXT_SCALE, COLOUR_TEXT_MUTED);
        return;
    }

    /* Lay the text out with hard wrapping, then show the last `rows` lines */
    int32_t line_start[NOTES_MAX + 2];
    int32_t line_len[NOTES_MAX + 2];
    int32_t num_lines = 0;
    int32_t start = 0;
    for (int32_t i = 0; i <= (int32_t)notes_len; i++) {
        if (i == (int32_t)notes_len || notes[i] == '\n' || i - start == cols) {
            line_start[num_lines] = start;
            line_len[num_lines] = i - start;
            num_lines++;
            start = (i < (int32_t)notes_len && notes[i] == '\n') ? i + 1 : i;
        }
    }

    int32_t first = MAX(0, num_lines - rows);
    int32_t y = frame.y + TITLE_HEIGHT + TEXT_PADDING;
    int32_t caret_x = frame.x + TEXT_PADDING;
    for (int32_t l = first; l < num_lines; l++) {
        char text[NOTES_MAX + 1];
        memcpy(text, &notes[line_start[l]], line_len[l]);
        text[line_len[l]] = '\0';
        caret_x = gfx_draw_text(&screen, frame.x + TEXT_PADDING, y, text, TEXT_SCALE, COLOUR_TEXT_DARK);
        y += LINE_HEIGHT;
    }

    if (focused) {
        gfx_fill_rect(&screen, (gfx_rect_t) { caret_x, y - LINE_HEIGHT + CHAR_HEIGHT, CHAR_WIDTH, 2 },
                      COLOUR_TEXT_DARK);
    }
}

static void draw_window(int id, bool focused)
{
    window_t *w = &windows[id];
    gfx_rect_t frame = w->frame;

    gfx_fill_rect(&screen, (gfx_rect_t) { frame.x + SHADOW_OFFSET, frame.y + SHADOW_OFFSET, frame.width, frame.height },
                  COLOUR_SHADOW);
    gfx_fill_rect(&screen, frame, COLOUR_WINDOW_BODY);

    gfx_fill_rect(&screen, (gfx_rect_t) { frame.x, frame.y, frame.width, TITLE_HEIGHT },
                  focused ? COLOUR_TITLE_ACTIVE : COLOUR_TITLE_INACTIVE);
    gfx_draw_text(&screen, frame.x + 8, frame.y + (TITLE_HEIGHT - CHAR_HEIGHT) / 2, w->title, TEXT_SCALE,
                  COLOUR_TEXT_LIGHT);
    gfx_fill_rect(&screen, close_button(frame), COLOUR_CLOSE);
    gfx_draw_rect(&screen, frame, 1, COLOUR_WINDOW_BORDER);

    switch (id) {
    case WIN_WELCOME:
        draw_welcome(frame);
        break;
    case WIN_DISPLAY:
        draw_display(frame);
        break;
    case WIN_NOTES:
        draw_notes(frame, focused);
        break;
    }
}

static void draw_clock(void)
{
    uint64_t secs = sddf_timer_time_now(timer_ch) / NS_IN_S;
    char text[32];
    sddf_snprintf(text, sizeof(text), "up %02lu:%02lu:%02lu", (unsigned long)(secs / 3600),
                  (unsigned long)((secs / 60) % 60), (unsigned long)(secs % 60));

    int32_t text_x = clock_rect.x + clock_rect.width - gfx_text_width(text, TEXT_SCALE);
    gfx_draw_text(&screen, text_x, clock_rect.y, text, TEXT_SCALE, COLOUR_TEXT_LIGHT);
}

static void draw_taskbar(int focused)
{
    int32_t w = (int32_t)screen.width;
    int32_t bar_y = (int32_t)screen.height - TASKBAR_HEIGHT;
    int32_t text_y = bar_y + (TASKBAR_HEIGHT - CHAR_HEIGHT) / 2;

    gfx_fill_rect(&screen, (gfx_rect_t) { 0, bar_y, w, TASKBAR_HEIGHT }, COLOUR_TASKBAR);
    gfx_fill_rect(&screen, (gfx_rect_t) { 0, bar_y, w, 1 }, COLOUR_TASKBAR_EDGE);

    gfx_rect_t start = { 6, bar_y + 5, gfx_text_width("Lions", TEXT_SCALE) + 20, TASKBAR_HEIGHT - 10 };
    gfx_fill_rect(&screen, start, COLOUR_BUTTON);
    gfx_draw_text(&screen, start.x + 10, text_y, "Lions", TEXT_SCALE, COLOUR_TEXT_DARK);

    for (int id = 0; id < NUM_WINDOWS; id++) {
        gfx_rect_t tab = tab_rects[id];
        if (id == focused) {
            gfx_fill_rect(&screen, tab, COLOUR_TAB_ACTIVE);
        }
        gfx_draw_text(&screen, tab.x + 8, text_y, windows[id].tab_label, TEXT_SCALE,
                      windows[id].visible ? COLOUR_TEXT_TAB : COLOUR_TEXT_TAB_HIDDEN);
    }

    draw_clock();
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

/* Redraw the whole scene, limited to rows [y0, y1) */
static void render(int32_t y0, int32_t y1)
{
    int32_t w = (int32_t)screen.width;
    int32_t h = (int32_t)screen.height;
    gfx_set_clip(&screen, (gfx_rect_t) { 0, y0, w, y1 - y0 });

    gfx_fill_vgradient(&screen, (gfx_rect_t) { 0, 0, w, h - TASKBAR_HEIGHT }, COLOUR_BG_TOP, COLOUR_BG_BOTTOM);
    gfx_draw_text(&screen, 24, h - TASKBAR_HEIGHT - 40, "LionsOS", 4, COLOUR_BG_LOGO);

    int focused = focused_window();
    draw_taskbar(focused);
    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (windows[z_order[i]].visible) {
            draw_window(z_order[i], z_order[i] == focused);
        }
    }
    draw_cursor();

    gfx_reset_clip(&screen);
}

/* Redraw and present everything that has been damaged since the last flush */
static void flush(void)
{
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

/* Scene updates */

static void raise_window(int id)
{
    int pos = 0;
    while (z_order[pos] != id) {
        pos++;
    }
    for (; pos < NUM_WINDOWS - 1; pos++) {
        z_order[pos] = z_order[pos + 1];
    }
    z_order[NUM_WINDOWS - 1] = id;
}

/* Focus changes redraw every visible window's title bar and the taskbar */
static void damage_focus_change(void)
{
    for (int id = 0; id < NUM_WINDOWS; id++) {
        if (windows[id].visible) {
            damage_window(id);
        }
    }
    damage_rows((int32_t)screen.height - TASKBAR_HEIGHT, TASKBAR_HEIGHT);
}

static void show_and_raise(int id)
{
    windows[id].visible = true;
    raise_window(id);
    damage_focus_change();
}

static void hide_window(int id)
{
    damage_focus_change();
    windows[id].visible = false;
    if (drag_window == id) {
        drag_window = -1;
    }
}

static void move_window(int id, int32_t x, int32_t y)
{
    int32_t w = (int32_t)screen.width;
    int32_t h = (int32_t)screen.height;
    gfx_rect_t *f = &windows[id].frame;

    /* Keep enough of the title bar on screen to grab it again */
    x = MAX(MIN(x, w - 40), 40 - f->width);
    y = MAX(MIN(y, h - TASKBAR_HEIGHT - TITLE_HEIGHT), 0);
    if (x == f->x && y == f->y) {
        return;
    }

    damage_window(id);
    f->x = x;
    f->y = y;
    damage_window(id);
}

static void pointer_moved(int32_t x, int32_t y)
{
    x = MAX(MIN(x, (int32_t)screen.width - 1), 0);
    y = MAX(MIN(y, (int32_t)screen.height - 1), 0);
    if (x == pointer_x && y == pointer_y) {
        return;
    }

    damage_cursor();
    damage_pointer_readout();
    pointer_x = x;
    pointer_y = y;
    damage_cursor();
    damage_pointer_readout();

    if (drag_window >= 0) {
        move_window(drag_window, pointer_x - drag_dx, pointer_y - drag_dy);
    }
}

static void pointer_pressed(void)
{
    for (int id = 0; id < NUM_WINDOWS; id++) {
        if (gfx_rect_contains(tab_rects[id], pointer_x, pointer_y)) {
            if (windows[id].visible && focused_window() == id) {
                hide_window(id);
            } else {
                show_and_raise(id);
            }
            return;
        }
    }

    for (int i = NUM_WINDOWS - 1; i >= 0; i--) {
        int id = z_order[i];
        gfx_rect_t frame = windows[id].frame;
        if (!windows[id].visible || !gfx_rect_contains(frame, pointer_x, pointer_y)) {
            continue;
        }

        if (gfx_rect_contains(close_button(frame), pointer_x, pointer_y)) {
            hide_window(id);
            return;
        }

        if (focused_window() != id) {
            show_and_raise(id);
        }
        if (pointer_y < frame.y + TITLE_HEIGHT) {
            drag_window = id;
            drag_dx = pointer_x - frame.x;
            drag_dy = pointer_y - frame.y;
        }
        return;
    }
}

static void key_pressed(uint16_t code)
{
    if (focused_window() != WIN_NOTES) {
        return;
    }

    if (code == INPUT_KEY_BACKSPACE) {
        if (notes_len > 0) {
            notes_len--;
        }
    } else if (code == INPUT_KEY_ENTER) {
        if (notes_len < NOTES_MAX) {
            notes[notes_len++] = '\n';
        }
    } else {
        char c = keymap_to_ascii(code, shift_left || shift_right);
        if (c == 0 || notes_len >= NOTES_MAX) {
            return;
        }
        notes[notes_len++] = c;
    }
    notes[notes_len] = '\0';
    damage_window(WIN_NOTES);
}

static void handle_input_event(input_event_t *ev)
{
    switch (ev->type) {
    case INPUT_EV_ABS:
        if (ev->code == INPUT_ABS_X) {
            pointer_moved(((int64_t)ev->value * (screen.width - 1)) / INPUT_ABS_MAX, pointer_y);
        } else if (ev->code == INPUT_ABS_Y) {
            pointer_moved(pointer_x, ((int64_t)ev->value * (screen.height - 1)) / INPUT_ABS_MAX);
        }
        break;
    case INPUT_EV_REL:
        if (ev->code == INPUT_REL_X) {
            pointer_moved(pointer_x + ev->value, pointer_y);
        } else if (ev->code == INPUT_REL_Y) {
            pointer_moved(pointer_x, pointer_y + ev->value);
        }
        break;
    case INPUT_EV_KEY:
        if (ev->code == INPUT_BTN_LEFT || ev->code == INPUT_BTN_TOUCH) {
            if (ev->value == INPUT_KEY_PRESSED) {
                pointer_pressed();
            } else if (ev->value == INPUT_KEY_RELEASED) {
                drag_window = -1;
            }
        } else if (ev->code == INPUT_KEY_LEFTSHIFT) {
            shift_left = ev->value != INPUT_KEY_RELEASED;
        } else if (ev->code == INPUT_KEY_RIGHTSHIFT) {
            shift_right = ev->value != INPUT_KEY_RELEASED;
        } else if (ev->value != INPUT_KEY_RELEASED) {
            key_pressed(ev->code);
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
    flush();
}

/* Setup */

static void layout(void)
{
    int32_t w = (int32_t)screen.width;
    int32_t h = (int32_t)screen.height;

    windows[WIN_WELCOME] = (window_t) {
        .title = "Welcome to LionsOS",
        .tab_label = "Welcome",
        .frame = { 40, 40, 500, TITLE_HEIGHT + 2 * TEXT_PADDING + 8 * LINE_HEIGHT },
        .visible = true,
    };

    gfx_rect_t welcome = windows[WIN_WELCOME].frame;
    gfx_rect_t display = { w - 340 - 40, 120, 340, TITLE_HEIGHT + 2 * TEXT_PADDING + 3 * LINE_HEIGHT };
    if (display.x < welcome.x + welcome.width + 20) {
        display.x = welcome.x + 60;
        display.y = welcome.y + welcome.height + 30;
    }
    windows[WIN_DISPLAY] = (window_t) {
        .title = "Display", .tab_label = "Display", .frame = display, .visible = true
    };

    gfx_rect_t notes_frame = { w - 420 - 40, h - TASKBAR_HEIGHT - 260 - 30, 420, 260 };
    windows[WIN_NOTES] = (window_t) {
        .title = "Notes", .tab_label = "Notes", .frame = notes_frame, .visible = true
    };

    int32_t bar_y = h - TASKBAR_HEIGHT;
    int32_t tab_x = 6 + gfx_text_width("Lions", TEXT_SCALE) + 20 + 12;
    for (int id = 0; id < NUM_WINDOWS; id++) {
        int32_t tab_w = gfx_text_width(windows[id].tab_label, TEXT_SCALE) + 16;
        tab_rects[id] = (gfx_rect_t) { tab_x, bar_y + 5, tab_w, TASKBAR_HEIGHT - 10 };
        tab_x += tab_w + 6;
    }

    int32_t clock_width = gfx_text_width("up 000:00:00", TEXT_SCALE);
    clock_rect = (gfx_rect_t) { w - clock_width - 12, bar_y + (TASKBAR_HEIGHT - CHAR_HEIGHT) / 2, clock_width,
                                CHAR_HEIGHT };

    pointer_x = w / 2;
    pointer_y = h / 2;
}

static bool setup_display(gpu_resp_get_display_info_t *info)
{
    if (info->num_scanouts == 0 || !info->scanouts[SCANOUT_ID].enabled) {
        LOG_DESKTOP_ERR("scanout %d is not available\n", SCANOUT_ID);
        return false;
    }

    scanout_rect = info->scanouts[SCANOUT_ID].rect;
    uint64_t bytes = (uint64_t)scanout_rect.width * scanout_rect.height * GPU_BPP_2D;
    if (bytes > FB_MAX_BYTES) {
        LOG_DESKTOP_ERR("scanout %ux%u needs %lu bytes but only %lu are available, "
                        "use a smaller mode or grow GPU_DATA_REGION_SIZE_CLI0\n",
                        scanout_rect.width, scanout_rect.height, (unsigned long)bytes, (unsigned long)FB_MAX_BYTES);
        return false;
    }

    screen = (gfx_surface_t) {
        .pixels = (uint32_t *)(gpu_data + FB_OFFSET),
        .width = scanout_rect.width,
        .height = scanout_rect.height,
        .stride = scanout_rect.width,
    };
    gfx_reset_clip(&screen);
    LOG_DESKTOP("scanout %d is %ux%u\n", SCANOUT_ID, screen.width, screen.height);

    layout();
    render(0, screen.height);

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
            LOG_DESKTOP_ERR("request %u failed with status %d\n", resp.id, resp.status);
            continue;
        }
        if (!is_display_info) {
            continue;
        }

        gpu_resp_get_display_info_t info;
        memcpy(&info, (void *)(gpu_data + DISPLAY_INFO_OFFSET), sizeof(info));

        if (!running) {
            running = setup_display(&info);
            if (running) {
                sddf_timer_set_timeout(timer_ch, CLOCK_TICK_NS);
            }
        } else if (info.scanouts[SCANOUT_ID].rect.width != scanout_rect.width
                   || info.scanouts[SCANOUT_ID].rect.height != scanout_rect.height) {
            /* TODO: recreate the resource at the new size */
            LOG_DESKTOP("display is now %ux%u, resizing is not supported yet\n",
                        info.scanouts[SCANOUT_ID].rect.width, info.scanouts[SCANOUT_ID].rect.height);
        }
    }
}

void init(void)
{
    assert(timer_config_check_magic(&timer_config));
    timer_ch = timer_config.driver_id;

    LOG_DESKTOP("starting\n");
    gpu_queue_init(&gpu_queue_handle, gpu_req_queue, gpu_resp_queue, GPU_QUEUE_CAPACITY_CLI0);
    input_queue_init(&input_handle, (input_queue_t *)input_queue, INPUT_QUEUE_CAPACITY(INPUT_QUEUE_REGION_SIZE));
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
    } else if (ch == timer_ch) {
        damage_rect(clock_rect);
        flush();
        sddf_timer_set_timeout(timer_ch, CLOCK_TICK_NS);
    } else {
        LOG_DESKTOP_ERR("notification on unexpected channel %u\n", ch);
    }
}
