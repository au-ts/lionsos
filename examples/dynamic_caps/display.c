/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Stands in for the compositor. It maps the surface read-only, as the
 * compositor maps an app's window, and shows what is in it when the manager
 * notifies it. Revoking the program's access to the surface does not touch
 * this mapping.
 */

#include <stdint.h>
#include <microkit.h>

uintptr_t surface;

static void putdec(uint64_t x)
{
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    do {
        buf[--i] = '0' + x % 10;
        x /= 10;
    } while (x);
    microkit_dbg_puts(buf + i);
}

void init(void)
{
}

void notified(microkit_channel ch)
{
    (void)ch;
    microkit_dbg_puts("DISPLAY: the surface shows ");
    putdec(*(volatile uint64_t *)surface);
    microkit_dbg_puts("\n");
}
