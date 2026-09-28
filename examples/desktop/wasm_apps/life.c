/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Conway's Game of Life. Click to toggle a cell, Space pauses, C clears and
 * R fills the board randomly.
 */

#include "lions.h"

#define CELL 8
#define COLS 60
#define ROWS 40
#define TOP 40

static unsigned char grid[ROWS][COLS], next[ROWS][COLS];
static int width, height;
static int paused;
static int generation;
static unsigned seed;

static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 16;
}

static void randomise(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            grid[r][c] = (rnd() % 4) == 0;
        }
    }
    generation = 0;
}

static void step(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr || dc) {
                        n += grid[(r + dr + ROWS) % ROWS][(c + dc + COLS) % COLS];
                    }
                }
            }
            next[r][c] = n == 3 || (n == 2 && grid[r][c]);
        }
    }
    memcpy(grid, next, sizeof(grid));
    generation++;
}

static void draw(void)
{
    char status[48];
    lions_fill_rect(0, 0, width, height, 0x10141c);
    snprintf(status, sizeof(status), "Generation %d%s", generation, paused ? "  (paused)" : "");
    lions_draw_text(12, 12, status, 2, 0xe6e6e6);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c]) {
                lions_fill_rect(c * CELL, TOP + r * CELL, CELL - 1, CELL - 1, 0x3a9d5d);
            }
        }
    }
    lions_commit(0, 0, width, height);
}

LIONS_EXPORT(app_init) void app_init(int w, int h)
{
    width = w;
    height = h;
    seed = (unsigned)lions_time_ms();
    randomise();
    draw();
    lions_set_tick(120);
}

LIONS_EXPORT(app_tick) void app_tick(int time_ms)
{
    (void)time_ms;
    if (!paused) {
        step();
        draw();
    }
}

LIONS_EXPORT(app_event) void app_event(int type, int code, int value, int x, int y)
{
    (void)code;
    if (type == LIONS_EV_POINTER_BUTTON && value == 1 && y >= TOP) {
        int c = x / CELL, r = (y - TOP) / CELL;
        if (c >= 0 && c < COLS && r >= 0 && r < ROWS) {
            grid[r][c] = !grid[r][c];
            draw();
        }
    } else if (type == LIONS_EV_KEY && value == 1) {
        if (x == ' ') {
            paused = !paused;
        } else if (x == 'c' || x == 'C') {
            memset(grid, 0, sizeof(grid));
            generation = 0;
        } else if (x == 'r' || x == 'R') {
            randomise();
        }
        draw();
    }
}
