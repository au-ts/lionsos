/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Sketch: draw with the pointer. Pick a colour or clear from the toolbar. */

#include <stdint.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <lions/input/input.h>
#include "gui_app.h"

#define WIDTH 400
#define HEIGHT 300
#define TOOLBAR_H 32
#define SWATCH 20
#define BRUSH 4

#define COLOUR_TOOLBAR GFX_RGB(0xe4, 0xe1, 0xda)
#define COLOUR_CANVAS GFX_RGB(0xff, 0xff, 0xff)
#define COLOUR_OUTLINE GFX_RGB(0x22, 0x22, 0x22)
#define COLOUR_TEXT GFX_RGB(0x22, 0x22, 0x22)

static const uint32_t palette[] = {
    GFX_RGB(0x22, 0x22, 0x22), GFX_RGB(0xd9, 0x4c, 0x3d), GFX_RGB(0xe0, 0x8a, 0x1e),
    GFX_RGB(0x3a, 0x9d, 0x5d), GFX_RGB(0x2f, 0x5d, 0xa8), GFX_RGB(0x8e, 0x4c, 0xb8),
};

static uint32_t colour_index;
static bool drawing;
static int32_t last_x, last_y;

/* Union of the areas changed while handling the current batch of events */
static gfx_rect_t damage;
static bool damaged;

static void add_damage(gfx_rect_t r)
{
    if (!damaged) {
        damage = r;
        damaged = true;
        return;
    }
    int32_t x0 = MIN(damage.x, r.x);
    int32_t y0 = MIN(damage.y, r.y);
    int32_t x1 = MAX(damage.x + damage.width, r.x + r.width);
    int32_t y1 = MAX(damage.y + damage.height, r.y + r.height);
    damage = (gfx_rect_t) { x0, y0, x1 - x0, y1 - y0 };
}

static gfx_rect_t swatch_rect(uint32_t i)
{
    return (gfx_rect_t) { 8 + (int32_t)i * (SWATCH + 8), (TOOLBAR_H - SWATCH) / 2, SWATCH, SWATCH };
}

static gfx_rect_t clear_rect(void)
{
    int32_t w = gfx_text_width("Clear", 2) + 16;
    return (gfx_rect_t) { WIDTH - w - 8, 4, w, TOOLBAR_H - 8 };
}

static void draw_toolbar(void)
{
    gfx_surface_t *s = gui_app_surface();
    gfx_fill_rect(s, (gfx_rect_t) { 0, 0, WIDTH, TOOLBAR_H }, COLOUR_TOOLBAR);
    for (uint32_t i = 0; i < ARRAY_SIZE(palette); i++) {
        gfx_rect_t r = swatch_rect(i);
        gfx_fill_rect(s, r, palette[i]);
        if (i == colour_index) {
            gfx_draw_rect(s, (gfx_rect_t) { r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, 2, COLOUR_OUTLINE);
        }
    }
    gfx_rect_t c = clear_rect();
    gfx_draw_rect(s, c, 1, COLOUR_OUTLINE);
    gfx_draw_text(s, c.x + 8, c.y + (c.height - 16) / 2, "Clear", 2, COLOUR_TEXT);
    add_damage((gfx_rect_t) { 0, 0, WIDTH, TOOLBAR_H });
}

static void clear_canvas(void)
{
    gfx_rect_t canvas = { 0, TOOLBAR_H, WIDTH, HEIGHT - TOOLBAR_H };
    gfx_fill_rect(gui_app_surface(), canvas, COLOUR_CANVAS);
    add_damage(canvas);
}

static void stroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    gfx_surface_t *s = gui_app_surface();
    gfx_set_clip(s, (gfx_rect_t) { 0, TOOLBAR_H, WIDTH, HEIGHT - TOOLBAR_H });

    /* Bresenham, stamping a square brush at every step */
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    int32_t x = x0, y = y0;
    for (;;) {
        gfx_fill_rect(s, (gfx_rect_t) { x - BRUSH / 2, y - BRUSH / 2, BRUSH, BRUSH }, palette[colour_index]);
        if (x == x1 && y == y1) {
            break;
        }
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }

    gfx_reset_clip(s);
    add_damage((gfx_rect_t) { MIN(x0, x1) - BRUSH, MIN(y0, y1) - BRUSH, (x1 > x0 ? x1 - x0 : x0 - x1) + 2 * BRUSH,
                              (y1 > y0 ? y1 - y0 : y0 - y1) + 2 * BRUSH });
}

static void pointer_button(gui_event_t *ev)
{
    if (ev->code != INPUT_BTN_LEFT) {
        return;
    }
    if (ev->value == INPUT_KEY_RELEASED) {
        drawing = false;
        return;
    }

    if (ev->y < TOOLBAR_H) {
        for (uint32_t i = 0; i < ARRAY_SIZE(palette); i++) {
            if (gfx_rect_contains(swatch_rect(i), ev->x, ev->y)) {
                colour_index = i;
                draw_toolbar();
            }
        }
        if (gfx_rect_contains(clear_rect(), ev->x, ev->y)) {
            clear_canvas();
        }
        return;
    }

    drawing = true;
    last_x = ev->x;
    last_y = ev->y;
    stroke(last_x, last_y, last_x, last_y);
}

void init(void)
{
    if (!gui_app_init("Sketch", WIDTH, HEIGHT)) {
        return;
    }
    draw_toolbar();
    clear_canvas();
    gfx_draw_text(gui_app_surface(), 12, TOOLBAR_H + 12, "Draw here!", 2, GFX_RGB(0xb0, 0xb4, 0xbb));
    gui_app_commit_all();
    damaged = false;
}

void notified(microkit_channel ch)
{
    if (ch != GUI_COMPOSITOR_CH) {
        return;
    }

    gui_event_t ev;
    while (gui_app_next_event(&ev)) {
        if (ev.type == GUI_EV_POINTER_BUTTON) {
            pointer_button(&ev);
        } else if (ev.type == GUI_EV_POINTER_MOTION && drawing) {
            stroke(last_x, last_y, ev.x, ev.y);
            last_x = ev.x;
            last_y = ev.y;
        }
    }

    if (damaged) {
        gui_app_commit(damage);
        damaged = false;
    }
}
