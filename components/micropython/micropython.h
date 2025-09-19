/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/serial/queue.h>
#include <microkit.h>
#include <libmicrokitco.h>

/* Right now since the framebuffer is not hooked up via a proper sDDF protocol
 * we hard-code the channel we expect to the framebuffer VMM. */
#ifdef ENABLE_FRAMEBUFFER
#define FRAMEBUFFER_VMM_CH 0
#endif

extern serial_queue_handle_t serial_rx_queue_handle;
extern serial_queue_handle_t serial_tx_queue_handle;

/* Configures the different wait behaviour of Micropython when keyboard
interrupts are received. */
typedef enum mp_cothread_wait_type {
    /**
     * Micropython will not be awoken until a notification is received on the
     * wait channel. Pending keyboard interrupts are not processed.
     */
    MP_WAIT_NO_INTERRUPT = 0,
    /**
     * Micropython will be awoken early if a keyboard interrupt is received. The
     * subsequent scheduled notification that was emulated will still be
     * received by the Micropython cothread.
     */
    MP_WAIT_RECV,
    /**
     * Micropython will be awoken early if a keyboard interrupt is received. The
     * subsequent scheduled notification that was emulated will be dropped.
     * NOTE: this will not stack to more than one notification drop is
     * Micropython is interrupted more than once.
     */
    MP_WAIT_DROP,
    /**
     * Micropython will be awoken early if a keyboard interrupt is received. The
     * subsequent scheduled notification that was emulated will be dropped,
     * unless the Micropython cothread waits on the channel again.
     */
    MP_WAIT_DROP_UNTIL_WAIT
} mp_cothread_wait_type_t;

/* Channel Micropython cothread is currently waiting on. */
extern microkit_channel mp_curr_wait_ch;

/**
 * Wrapper over `microkit_cothread_wait_on_channel` allowing the Micropython
 * cothread to wait on a channel and possibly handle keyboard interrupts. Allows
 * the caller to configure how a keyboard interrupt received during this wait
 * should be handled.
 *
 * If `handle_interrupt` is not set to MP_WAIT_NO_INTERRUPT, the Micropython
 * cothread will be awoken if a keyboard interrupt is received. Upon being
 * awoken from the wait, `mp_handle_pending` is called to process pending events
 * (keyboard interrupts).
 *
 * @param ch channel the Micropython cothread wishes to wait on.
 * @param handle_interrupt how the subsequently scheduled interrupt should be
 * handled if the Micropython cothread is interrupted during this wait.
 */
void mp_cothread_wait(microkit_channel ch,
                      mp_cothread_wait_type_t handle_interrupt);

/**
 * Awaken the Micropython cothread early from its wait. No effect if the
 * cothread is currently configured to ignore interrupts.
 */
void mp_cothread_interrupt(void);

/**
 * Wrappper over `microkit_cothread_recv_ntfn` allowing the Micropython cothread
 * to be awoken if it is waiting on a channel, unless the next notification on
 * this channel is to be ignored.
 *
 * @param ch channel the Micropython cothread receives on.
 */
void mp_cothread_maybe_recv(microkit_channel ch);
