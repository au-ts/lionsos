/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include "py/runtime.h"
#include "micropython.h"
#include "mphalport.h"

microkit_channel mp_curr_wait_ch;

typedef struct mp_cothread_ch_state {
    bool drop;
    mp_cothread_wait_type_t type;
} mp_cothread_ch_state_t;

mp_cothread_ch_state_t mp_channels[MICROKIT_MAX_CHANNELS];

void mp_cothread_wait(microkit_channel ch,
                      mp_cothread_wait_type_t handle_interrupt)
{
    if (mp_channels[ch].type == MP_WAIT_DROP_UNTIL_WAIT) {
        mp_channels[ch].drop = false;
    }
    
    mp_channels[ch].type = handle_interrupt;
    mp_curr_wait_ch = ch;
    microkit_cothread_wait_on_channel(ch);

    if (handle_interrupt != MP_WAIT_NO_INTERRUPT) {
        /* Ensure interrupts received while waiting are processed and raised. */
        mp_handle_pending(true);
    }
}

void mp_cothread_interrupt(void)
{
    if (mp_channels[mp_curr_wait_ch].type == MP_WAIT_NO_INTERRUPT) {
        return;
    }
    
    if (mp_channels[mp_curr_wait_ch].type == MP_WAIT_DROP ||
        mp_channels[mp_curr_wait_ch].type == MP_WAIT_DROP_UNTIL_WAIT) {
        mp_channels[mp_curr_wait_ch].drop = true;
    }

    microkit_cothread_recv_ntfn(mp_curr_wait_ch);
}

void mp_cothread_maybe_recv(microkit_channel ch)
{
    if (mp_channels[ch].drop) {
        mp_channels[ch].drop = false;
        return;
    }

    microkit_cothread_recv_ntfn(ch);
}
