/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * An app that stops responding: a click makes it compute forever. Only on
 * the disk of a SANDBOX=1 build, where it runs in a sandbox below every
 * other PD's priority, so the host still gets Esc and stops it. Run inside
 * the host PD, it would hang the host.
 */

#include "lions.h"

static int win;
static int width, height;
static volatile unsigned counter;

LIONS_EXPORT(app_init) void app_init(int w, int h)
{
    win = lions_cap("window");
    width = w;
    height = h;
    lions_fill_rect(win, 0, 0, width, height, 0x1b1f27);
    lions_draw_text(win, 20, 24, "Click to spin forever", 2, 0xe6e6e6);
    lions_draw_text(win, 20, 64, "The app then never gives", 2, 0x8a8f99);
    lions_draw_text(win, 20, 86, "control back. Esc still", 2, 0x8a8f99);
    lions_draw_text(win, 20, 108, "works: the sandbox runs", 2, 0x8a8f99);
    lions_draw_text(win, 20, 130, "below every other PD, and", 2, 0x8a8f99);
    lions_draw_text(win, 20, 152, "the host stops it.", 2, 0x8a8f99);
    lions_commit(win, 0, 0, width, height);
}

LIONS_EXPORT(app_event) void app_event(int type, int code, int value, int x, int y)
{
    (void)code;
    (void)x;
    (void)y;
    if (type == LIONS_EV_POINTER_BUTTON && value == 1) {
        for (;;) {
            counter++;
        }
    }
}
