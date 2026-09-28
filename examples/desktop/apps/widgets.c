/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Widgets: a gallery of microui controls running in their own PD. */

#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include "mu_app.h"

#define WIDTH 400
#define HEIGHT 360

static int clicks;
static int checked = 1;
static float red = 224, green = 138, blue = 30;
static float count = 3;
static char name[32] = "seL4";

static void frame(mu_Context *ctx)
{
    char text[64];

    mu_layout_row(ctx, 2, (int[]) { 136, -1 }, 0);
    if (mu_button(ctx, "Click me")) {
        clicks++;
    }
    sddf_snprintf(text, sizeof(text), "Clicked %d times", clicks);
    mu_label(ctx, text);

    mu_layout_row(ctx, 1, (int[]) { -1 }, 0);
    mu_checkbox(ctx, "A checkbox", &checked);

    /* Colour sliders with a live preview */
    mu_layout_row(ctx, 2, (int[]) { 260, -1 }, 0);
    mu_layout_begin_column(ctx);
    mu_layout_row(ctx, 2, (int[]) { 30, -1 }, 0);
    mu_label(ctx, "R:");
    mu_slider(ctx, &red, 0, 255);
    mu_label(ctx, "G:");
    mu_slider(ctx, &green, 0, 255);
    mu_label(ctx, "B:");
    mu_slider(ctx, &blue, 0, 255);
    mu_layout_end_column(ctx);
    mu_Rect preview = mu_layout_next(ctx);
    mu_draw_rect(ctx, preview, mu_color(red, green, blue, 255));

    mu_layout_row(ctx, 2, (int[]) { 80, -1 }, 0);
    mu_label(ctx, "Name:");
    mu_textbox(ctx, name, sizeof(name));
    mu_layout_row(ctx, 1, (int[]) { -1 }, 0);
    sddf_snprintf(text, sizeof(text), "Hello, %s!", name);
    mu_label(ctx, text);

    mu_layout_row(ctx, 2, (int[]) { 80, 120 }, 0);
    mu_label(ctx, "Count:");
    mu_number(ctx, &count, 1);

    mu_layout_row(ctx, 1, (int[]) { -1 }, 0);
    if (mu_header_ex(ctx, "About this PD", 0)) {
        mu_label(ctx, "Drawn by microui in a");
        mu_label(ctx, "separate protection domain.");
    }
    (void)checked;
}

void init(void)
{
    mu_app_init("Widgets", WIDTH, HEIGHT, frame, NULL);
}

void notified(microkit_channel ch)
{
    if (ch == GUI_COMPOSITOR_CH) {
        mu_app_handle_events();
    }
}
