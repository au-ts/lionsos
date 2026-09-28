/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The LionsOS desktop, milestone 2: a single native client of the sDDF GPU
 * virtualiser. It software-renders a desktop into its GPU data region, scans
 * it out as a 2D resource, and then once a second redraws the taskbar clock,
 * sending only the damaged rows to the device.
 *
 * Nothing here involves a driver VM: desktop -> gpu_virt -> virtio-gpu driver.
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
#include <gpu_config.h>
#include "gfx.h"

#define LOG_DESKTOP(...) do{ sddf_dprintf("DESKTOP|INFO: "); sddf_dprintf(__VA_ARGS__); }while(0)
#define LOG_DESKTOP_ERR(...) do{ sddf_dprintf("DESKTOP|ERROR: "); sddf_dprintf(__VA_ARGS__); }while(0)

/* Fixed by meta.py, the timer channel comes from the generated config */
#define VIRT_CH 0

#define DISPLAY_INFO_OFFSET 0
#define FB_OFFSET 0x1000
#define FB_MAX_BYTES (GPU_DATA_REGION_SIZE_CLI0 - FB_OFFSET)

#define SCANOUT_ID 0
#define FB_RESOURCE_ID 1

#define CLOCK_TICK_NS NS_IN_S

/* Theme */
#define COLOUR_BG_TOP GFX_RGB(0x14, 0x2a, 0x4f)
#define COLOUR_BG_BOTTOM GFX_RGB(0x2e, 0x7d, 0x8c)
#define COLOUR_TASKBAR GFX_RGB(0x1b, 0x1f, 0x27)
#define COLOUR_TASKBAR_EDGE GFX_RGB(0x3a, 0x41, 0x4f)
#define COLOUR_BUTTON GFX_RGB(0xe0, 0x8a, 0x1e)
#define COLOUR_TEXT_LIGHT GFX_RGB(0xf2, 0xf2, 0xf2)
#define COLOUR_TEXT_DARK GFX_RGB(0x22, 0x22, 0x22)
#define COLOUR_TEXT_MUTED GFX_RGB(0x5a, 0x5f, 0x69)
#define COLOUR_WINDOW_BODY GFX_RGB(0xf7, 0xf5, 0xf0)
#define COLOUR_WINDOW_BORDER GFX_RGB(0x0f, 0x14, 0x1c)
#define COLOUR_TITLE_ACTIVE GFX_RGB(0x2f, 0x5d, 0xa8)
#define COLOUR_TITLE_INACTIVE GFX_RGB(0x6b, 0x72, 0x80)
#define COLOUR_SHADOW GFX_RGB(0x0a, 0x12, 0x20)
#define COLOUR_CLOSE GFX_RGB(0xd9, 0x4c, 0x3d)

#define TASKBAR_HEIGHT 36
#define TITLE_HEIGHT 24
#define TEXT_SCALE 2
#define LINE_HEIGHT (GFX_FONT_HEIGHT * TEXT_SCALE + 6)

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

gpu_events_t *gpu_events;
gpu_req_queue_t *gpu_req_queue;
gpu_resp_queue_t *gpu_resp_queue;
uintptr_t gpu_data;

static sddf_channel timer_ch;
static gpu_queue_handle_t gpu_queue_handle;
static uint32_t next_req_id = 0;
static uint32_t display_info_req_id = UINT32_MAX;
static bool display_info_pending = false;
static bool running = false;

static gpu_rect_t scanout_rect;
static gfx_surface_t screen;
static gfx_rect_t clock_rect;

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

static void draw_window(gfx_rect_t frame, const char *title, bool active, const char *const *lines,
                        uint32_t num_lines)
{
    gfx_fill_rect(&screen, (gfx_rect_t) { frame.x + 6, frame.y + 6, frame.width, frame.height }, COLOUR_SHADOW);
    gfx_fill_rect(&screen, frame, COLOUR_WINDOW_BODY);

    gfx_rect_t title_bar = { frame.x, frame.y, frame.width, TITLE_HEIGHT };
    gfx_fill_rect(&screen, title_bar, active ? COLOUR_TITLE_ACTIVE : COLOUR_TITLE_INACTIVE);
    gfx_draw_text(&screen, frame.x + 8, frame.y + (TITLE_HEIGHT - GFX_FONT_HEIGHT * TEXT_SCALE) / 2, title,
                  TEXT_SCALE, COLOUR_TEXT_LIGHT);
    gfx_fill_rect(&screen, (gfx_rect_t) { frame.x + frame.width - 20, frame.y + 6, 12, 12 }, COLOUR_CLOSE);

    gfx_draw_rect(&screen, frame, 1, COLOUR_WINDOW_BORDER);

    int32_t y = frame.y + TITLE_HEIGHT + 12;
    for (uint32_t i = 0; i < num_lines; i++) {
        uint32_t colour = lines[i][0] == ' ' ? COLOUR_TEXT_MUTED : COLOUR_TEXT_DARK;
        gfx_draw_text(&screen, frame.x + 12, y, lines[i], TEXT_SCALE, colour);
        y += LINE_HEIGHT;
    }
}

static void draw_clock(void)
{
    uint64_t secs = sddf_timer_time_now(timer_ch) / NS_IN_S;
    char text[32];
    sddf_snprintf(text, sizeof(text), "up %02lu:%02lu:%02lu", (unsigned long)(secs / 3600),
                  (unsigned long)((secs / 60) % 60), (unsigned long)(secs % 60));

    gfx_fill_rect(&screen, clock_rect, COLOUR_TASKBAR);
    int32_t text_x = clock_rect.x + clock_rect.width - gfx_text_width(text, TEXT_SCALE);
    gfx_draw_text(&screen, text_x, clock_rect.y, text, TEXT_SCALE, COLOUR_TEXT_LIGHT);
}

static void draw_desktop(void)
{
    int32_t w = (int32_t)screen.width;
    int32_t h = (int32_t)screen.height;

    /* Wallpaper */
    gfx_fill_vgradient(&screen, (gfx_rect_t) { 0, 0, w, h - TASKBAR_HEIGHT }, COLOUR_BG_TOP, COLOUR_BG_BOTTOM);
    gfx_draw_text(&screen, 24, h - TASKBAR_HEIGHT - 40, "LionsOS", 4, GFX_RGB(0x9f, 0xc4, 0xd1));

    /* Taskbar */
    int32_t bar_y = h - TASKBAR_HEIGHT;
    gfx_fill_rect(&screen, (gfx_rect_t) { 0, bar_y, w, TASKBAR_HEIGHT }, COLOUR_TASKBAR);
    gfx_fill_rect(&screen, (gfx_rect_t) { 0, bar_y, w, 1 }, COLOUR_TASKBAR_EDGE);

    int32_t text_y = bar_y + (TASKBAR_HEIGHT - GFX_FONT_HEIGHT * TEXT_SCALE) / 2;
    gfx_rect_t start = { 6, bar_y + 5, gfx_text_width("Lions", TEXT_SCALE) + 20, TASKBAR_HEIGHT - 10 };
    gfx_fill_rect(&screen, start, COLOUR_BUTTON);
    gfx_draw_text(&screen, start.x + 10, text_y, "Lions", TEXT_SCALE, COLOUR_TEXT_DARK);

    int32_t tab_x = start.x + start.width + 12;
    gfx_draw_text(&screen, tab_x, text_y, "Welcome   Display", TEXT_SCALE, GFX_RGB(0xb8, 0xbe, 0xc9));

    int32_t clock_width = gfx_text_width("up 000:00:00", TEXT_SCALE);
    clock_rect = (gfx_rect_t) { w - clock_width - 12, text_y, clock_width, GFX_FONT_HEIGHT * TEXT_SCALE };
    draw_clock();

    /* Placeholder windows. In milestone 4 these become surfaces owned by client PDs. */
    static const char *const welcome[] = {
        "A native desktop on seL4.",
        "",
        "No Linux and no driver VM:",
        " desktop -> gpu_virt",
        "         -> virtio-gpu",
        "",
        "Every window here is still",
        "drawn by this one PD.",
    };
    gfx_rect_t welcome_frame = { 40, 40, 460, TITLE_HEIGHT + 24 + LINE_HEIGHT * ARRAY_SIZE(welcome) };
    draw_window(welcome_frame, "Welcome to LionsOS", true, welcome, ARRAY_SIZE(welcome));

    char resolution[40];
    sddf_snprintf(resolution, sizeof(resolution), "Mode:    %ux%u", screen.width, screen.height);
    char memory[40];
    sddf_snprintf(memory, sizeof(memory), "Backing: %u KiB", (screen.stride * screen.height * GPU_BPP_2D) / 1024);
    const char *const display[] = {
        resolution,
        "Format:  B8G8R8A8",
        memory,
        "Scanout: 0",
    };
    /* Beside the welcome window if there is room, otherwise below it */
    gfx_rect_t display_frame = { w - 340 - 40, 120, 340, TITLE_HEIGHT + 24 + LINE_HEIGHT * ARRAY_SIZE(display) };
    if (display_frame.x < welcome_frame.x + welcome_frame.width + 20) {
        display_frame.x = welcome_frame.x + 60;
        display_frame.y = welcome_frame.y + welcome_frame.height + 30;
    }
    draw_window(display_frame, "Display", false, display, ARRAY_SIZE(display));
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
    LOG_DESKTOP("scanout %d is %ux%u\n", SCANOUT_ID, screen.width, screen.height);

    draw_desktop();

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

static void handle_responses(void)
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
    request_display_info();
}

void notified(microkit_channel ch)
{
    if (ch == VIRT_CH) {
        handle_responses();
        if (gpu_events_check_display_info(gpu_events) && !display_info_pending) {
            gpu_events_clear_display_info(gpu_events);
            request_display_info();
        }
    } else if (ch == timer_ch) {
        draw_clock();
        present_rows(clock_rect.y, clock_rect.height);
        sddf_timer_set_timeout(timer_ch, CLOCK_TICK_NS);
    } else {
        LOG_DESKTOP_ERR("notification on unexpected channel %u\n", ch);
    }
}
