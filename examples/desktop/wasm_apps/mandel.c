/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The Mandelbrot set, rendered a few rows per tick so the window stays
 * responsive. Left click zooms in on a point, right click zooms out, R resets.
 */

#include "lions.h"

#define MAX_WIDTH 640
#define ROWS_PER_TICK 8
#define MAX_ITER 48

static int win, timer, console;
static int width, height;
static double cx = -0.6, cy = 0.0, scale = 3.2;
static int next_row;
static unsigned row_buf[MAX_WIDTH * ROWS_PER_TICK];

static unsigned colour(int i)
{
    if (i >= MAX_ITER) {
        return 0x10141c;
    }
    int t = i * 255 / MAX_ITER;
    int r = (t * 3) > 255 ? 255 : t * 3;
    int g = t;
    int b = 80 + t / 2;
    return (unsigned)(r << 16 | g << 8 | b);
}

static void render_rows(void)
{
    int rows = height - next_row < ROWS_PER_TICK ? height - next_row : ROWS_PER_TICK;
    double step = scale / width;
    for (int ry = 0; ry < rows; ry++) {
        double y0 = cy + (next_row + ry - height / 2) * step;
        for (int px = 0; px < width; px++) {
            double x0 = cx + (px - width / 2) * step;
            double x = 0, y = 0;
            int i = 0;
            while (i < MAX_ITER && x * x + y * y <= 4.0) {
                double xt = x * x - y * y + x0;
                y = 2 * x * y + y0;
                x = xt;
                i++;
            }
            row_buf[ry * width + px] = colour(i);
        }
    }
    lions_blit(win, 0, next_row, width, rows, row_buf, (unsigned)(width * rows * 4));
    lions_commit(win, 0, next_row, width, rows);
    next_row += rows;
    if (next_row >= height) {
        lions_timer_start(timer, 0);
        lions_log(console, "frame done");
    }
}

static void restart(void)
{
    next_row = 0;
    lions_timer_start(timer, 10);
}

LIONS_EXPORT(app_init) void app_init(int w, int h)
{
    win = lions_cap("window");
    timer = lions_cap("timer");
    console = lions_cap("console");
    width = w > MAX_WIDTH ? MAX_WIDTH : w;
    height = h;
    restart();
}

LIONS_EXPORT(app_tick) void app_tick(int time_ms)
{
    (void)time_ms;
    if (next_row < height) {
        render_rows();
    }
}

LIONS_EXPORT(app_event) void app_event(int type, int code, int value, int x, int y)
{
    if (type == LIONS_EV_POINTER_BUTTON && value == 1) {
        double step = scale / width;
        cx += (x - width / 2) * step;
        cy += (y - height / 2) * step;
        scale = code == LIONS_BTN_RIGHT ? scale * 2 : scale / 2;
        restart();
    } else if (type == LIONS_EV_KEY && value == 1 && (x == 'r' || x == 'R')) {
        cx = -0.6;
        cy = 0.0;
        scale = 3.2;
        restart();
    }
}
