/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * An empty PD. It holds nothing of interest, and the manager replaces what
 * it runs with a program the manager loads at run time.
 */

#include <microkit.h>

void init(void)
{
    microkit_dbg_puts("SANDBOX: empty, waiting for a program\n");
}

void notified(microkit_channel ch)
{
    (void)ch;
}
