/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The sandbox PD's own program, which never runs an app: the host replaces
 * what the sandbox runs with the runner, loaded at run time (see sandbox.h).
 * The PD exists for its TCB, scheduling context and channel to the host.
 */

#include <microkit.h>

void init(void)
{
}

void notified(microkit_channel ch)
{
    (void)ch;
}
