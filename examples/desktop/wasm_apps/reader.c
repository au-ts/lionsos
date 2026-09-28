/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Reader: shows the one file it has been granted, /apps/readme.txt. */

#include "lions.h"

#define TEXT_MAX 4096
#define COLS 56
#define LINE_H 12
#define TOP 44

static int win;
static char text[TEXT_MAX + 1];

LIONS_EXPORT(app_init) void app_init(int width, int height)
{
    win = lions_cap("window");
    int file = lions_cap("file /apps/readme.txt");

    lions_fill_rect(win, 0, 0, width, height, 0x1b1f27);
    lions_draw_text(win, 16, 14, "/apps/readme.txt", 2, 0xe08a1e);

    int len = lions_file_read(file, 0, text, TEXT_MAX);
    if (len < 0) {
        lions_draw_text(win, 16, TOP, "No capability for the file.", 2, 0xd94c3d);
        lions_commit(win, 0, 0, width, height);
        return;
    }
    text[len] = '\0';

    /* Hard-wrap at COLS characters and at newlines */
    int y = TOP;
    int i = 0;
    while (text[i] != '\0' && y + LINE_H <= height - 8) {
        char line[COLS + 1];
        int n = 0;
        while (text[i] != '\0' && text[i] != '\n' && n < COLS) {
            line[n++] = text[i++];
        }
        if (text[i] == '\n') {
            i++;
        }
        line[n] = '\0';
        lions_draw_text(win, 16, y, line, 1, 0xe6e6e6);
        y += LINE_H;
    }
    lions_commit(win, 0, 0, width, height);
}
