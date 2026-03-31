/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "modmachine.h"

#ifdef MICROPY_PY_MACHINE

void mp_machine_idle(void) {
    MICROPY_EVENT_POLL_HOOK;
}

#endif /* MICROPY_PY_MACHINE */
