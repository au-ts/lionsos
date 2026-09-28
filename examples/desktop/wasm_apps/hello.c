/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* The smallest useful app: text, clicks and typing. */

#include "lions.h"

static int width, height;
static int clicks;
static char typed[28];
static int typed_len;

static void draw(void)
{
    char line[48];
    lions_fill_rect(0, 0, width, height, 0x1b1f27);
    lions_draw_text(20, 24, "Hello from", 3, 0xe6e6e6);
    lions_draw_text(20, 56, "WebAssembly!", 3, 0xe08a1e);
    lions_draw_text(20, 110, "Loaded from the FAT disk at", 2, 0x8a8f99);
    lions_draw_text(20, 132, "run time and interpreted", 2, 0x8a8f99);
    lions_draw_text(20, 154, "by WAMR in its own PD.", 2, 0x8a8f99);

    snprintf(line, sizeof(line), "Clicks: %d", clicks);
    lions_draw_text(20, 200, line, 2, 0xe6e6e6);
    lions_draw_text(20, 230, "Typed:", 2, 0xe6e6e6);
    lions_draw_text(124, 230, typed, 2, 0x3a9d5d);
    lions_draw_text(20, height - 30, "Esc returns to the list", 2, 0x8a8f99);
    lions_commit(0, 0, width, height);
}

LIONS_EXPORT(app_init) void app_init(int w, int h)
{
    width = w;
    height = h;
    lions_log("hello, world");
    draw();
}

LIONS_EXPORT(app_event) void app_event(int type, int code, int value, int x, int y)
{
    (void)code;
    (void)y;
    if (type == LIONS_EV_POINTER_BUTTON && value == 1) {
        clicks++;
        draw();
    } else if (type == LIONS_EV_KEY && value != 0 && x != 0) {
        if (typed_len == (int)sizeof(typed) - 1) {
            typed_len = 0;
        }
        typed[typed_len++] = (char)x;
        typed[typed_len] = '\0';
        draw();
    }
}
