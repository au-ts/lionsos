/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Clock: shows the system uptime, redrawing itself once a second. */

#include <stdint.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include "gui_app.h"

#define WIDTH 360
#define HEIGHT 110
#define DIGIT_SCALE 5

#define COLOUR_BG GFX_RGB(0x1b, 0x1f, 0x27)
#define COLOUR_DIGITS GFX_RGB(0xe0, 0x8a, 0x1e)
#define COLOUR_CAPTION GFX_RGB(0x8a, 0x8f, 0x99)

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

static sddf_channel timer_ch;
static gfx_rect_t digits_rect;

static void draw_time(void)
{
    gfx_surface_t *s = gui_app_surface();
    uint64_t secs = sddf_timer_time_now(timer_ch) / NS_IN_S;
    char text[16];
    sddf_snprintf(text, sizeof(text), "%02lu:%02lu:%02lu", (unsigned long)(secs / 3600) % 100,
                  (unsigned long)((secs / 60) % 60), (unsigned long)(secs % 60));

    gfx_fill_rect(s, digits_rect, COLOUR_BG);
    gfx_draw_text(s, digits_rect.x, digits_rect.y, text, DIGIT_SCALE, COLOUR_DIGITS);
    gui_app_commit(digits_rect);
}

void init(void)
{
    assert(timer_config_check_magic(&timer_config));
    timer_ch = timer_config.driver_id;

    if (!gui_app_init("Clock", WIDTH, HEIGHT)) {
        return;
    }

    gfx_surface_t *s = gui_app_surface();
    gfx_fill_rect(s, (gfx_rect_t) { 0, 0, WIDTH, HEIGHT }, COLOUR_BG);
    int32_t digits_w = gfx_text_width("00:00:00", DIGIT_SCALE);
    digits_rect = (gfx_rect_t) { (WIDTH - digits_w) / 2, 18, digits_w, GFX_FONT_HEIGHT * DIGIT_SCALE };
    const char *caption = "uptime";
    gfx_draw_text(s, (WIDTH - gfx_text_width(caption, 2)) / 2, HEIGHT - 30, caption, 2, COLOUR_CAPTION);
    gui_app_commit_all();

    draw_time();
    sddf_timer_set_timeout(timer_ch, NS_IN_S);
}

void notified(microkit_channel ch)
{
    if (ch == timer_ch) {
        draw_time();
        sddf_timer_set_timeout(timer_ch, NS_IN_S);
    } else if (ch == GUI_COMPOSITOR_CH) {
        /* The clock ignores input, but drains it so the queue never fills */
        gui_event_t ev;
        while (gui_app_next_event(&ev)) {
        }
    }
}
