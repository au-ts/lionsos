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

extern serial_client_config_t serial_config;

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    char c;

    // Wait for a notification from the RX virtualiser if we do not have
    // any data to process.

    // This is in a loop because the notification for a particular
    // string may only be delivered after we have already consumed it.
    while(serial_queue_empty(&serial_rx_queue_handle,
                             serial_rx_queue_handle.queue->head)) {
        microkit_cothread_wait_on_channel(serial_config.rx.id);
    }

    // Dequeue and return character
    int ret = serial_dequeue(&serial_rx_queue_handle, &c);
    assert(!ret);

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
