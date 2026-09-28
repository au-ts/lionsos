/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Probe: tries to reach things it was not granted and shows what happens.
 * Its .caps file also asks for a file outside /apps and for "network", which
 * the host's policy refuses before the app even starts.
 */

#include "lions.h"

static int win;
static int y;
static int blocked, leaked;

static void report(const char *what, int ok, int expected_ok)
{
    int as_expected = (ok != 0) == (expected_ok != 0);
    if (!as_expected) {
        leaked++;
    } else if (!expected_ok) {
        blocked++;
    }
    lions_draw_text(win, 16, y, what, 1, 0xe6e6e6);
    lions_draw_text(win, 360, y, ok ? "allowed" : "denied", 1,
                    as_expected ? (ok ? 0x3a9d5d : 0xe08a1e) : 0xd94c3d);
    y += 16;
}

LIONS_EXPORT(app_init) void app_init(int width, int height)
{
    char buf[16];
    win = lions_cap("window");
    int console = lions_cap("console");

    lions_fill_rect(win, 0, 0, width, height, 0x1b1f27);
    lions_draw_text(win, 16, 14, "Capability probe", 2, 0xe08a1e);
    y = 48;

    report("draw in my window", win >= 0, 1);
    report("log to the console", lions_log(console, "probe: starting") == 0, 1);
    report("get /secret.txt (outside /apps)", lions_cap("file /secret.txt") >= 0, 0);
    report("get the network", lions_cap("network") >= 0, 0);
    report("get /apps/readme.txt (not requested)", lions_cap("file /apps/readme.txt") >= 0, 0);

    int forged = 0;
    for (int h = 0; h < 8; h++) {
        if (lions_file_read(h, 0, buf, sizeof(buf)) >= 0) {
            forged = 1;
        }
    }
    report("read a file through handles 0..7", forged, 0);
    report("use my window as a timer", lions_timer_start(win, 100) == 0, 0);

    lions_cap_drop(console);
    report("log after dropping the console", lions_log(console, "probe: still here?") == 0, 0);

    char summary[64];
    snprintf(summary, sizeof(summary), "%d attempts blocked, %d leaked", blocked, leaked);
    lions_draw_text(win, 16, y + 12, summary, 2, leaked ? 0xd94c3d : 0x3a9d5d);
    lions_draw_text(win, 16, height - 26, "See AUDIT lines on the console", 1, 0x8a8f99);
    lions_commit(win, 0, 0, width, height);
}
