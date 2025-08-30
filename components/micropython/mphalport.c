/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include "micropython.h"
#include "py/mpconfig.h"
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include "py/stream.h"
#include "py/ringbuf.h"
#include "shared/runtime/interrupt_char.h"

extern serial_client_config_t serial_config;

/* This is the internal micropython ringbuffer we will copy into from our sDDF
ringbuffers. */

static uint8_t stdin_ringbuf_array[1024];
ringbuf_t stdin_ringbuf = { stdin_ringbuf_array, sizeof(stdin_ringbuf_array) };

// Process all the characters in the sddf queues and add them to our internal micropython array.
// This is so that we can intercept ctrl+c interrupts here, rather than wait for micropython
// to call mp_hal_stdin_rx_chr before we can consume ctrl+c.
bool process_sddf_rx_char(void) {
    while(!serial_queue_empty(&serial_rx_queue_handle,
                             serial_rx_queue_handle.queue->head)) {
        char ch;
        int ret = serial_dequeue(&serial_rx_queue_handle, &ch);
        assert(!ret);

        if (ch == mp_interrupt_char) {
            mp_sched_keyboard_interrupt();
            /* Delete all previous buffer entries. */
            stdin_ringbuf.iget = 0;
            stdin_ringbuf.iput = 0;
            return true;
        }
        /* Add this character to our MP stdin_ringbuf. */
        ret = ringbuf_put(&stdin_ringbuf, ch);
        assert(ret == 0);
    }

    return false;
}

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    /* We will process all data in our MP stdin_ringbuf, if that is empty
    and we are still attempting to read, we will await a notif from the rx
    multiplexer. */
    int c = 0;

    /* We will await on a serial event here. Once the serial event has occured, we should have populated the internal ringbuffer with a character. */
    while (ringbuf_peek(&stdin_ringbuf) == -1) {
        microkit_cothread_wait_on_channel(serial_config.rx.id);
        /* This handles any interrupts that have been raised whilst
        the t_event co-routine was running. */
        mp_handle_pending(true);
    }

    c = ringbuf_get(&stdin_ringbuf);

    /* Process the sDDF queues again, in the case that we have buffered our input
    in them. */
    process_sddf_rx_char();

    return c;
}



// Send the string of given length.
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len)
{
    for (;;) {
        uint32_t n = serial_enqueue_batch(&serial_tx_queue_handle, len, str);
        if (n != 0) {
            microkit_notify(serial_config.tx.id);
        }
        len -= n;
        if (len == 0) {
            break;
        }

        serial_request_consumer_signal(&serial_tx_queue_handle);
        if (serial_queue_full(&serial_tx_queue_handle, serial_tx_queue_handle.queue->tail)) {
            microkit_cothread_wait_on_channel(serial_config.tx.id);
            /* This handles any interrupts that have been raised whilst
            the main cothread has been running. */
            mp_handle_pending(true);
        } else {
            serial_cancel_consumer_signal(&serial_tx_queue_handle);
        }
    }
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if ((poll_flags & MP_STREAM_POLL_RD) && !serial_queue_empty(&serial_rx_queue_handle, serial_rx_queue_handle.queue->head)) {
        ret |= MP_STREAM_POLL_RD;
    }
    if ((poll_flags & MP_STREAM_POLL_WR) && !serial_queue_full(&serial_tx_queue_handle, serial_tx_queue_handle.queue->tail)) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}
