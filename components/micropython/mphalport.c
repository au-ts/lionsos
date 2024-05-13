#include <microkit.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include "micropython.h"
#include "py/mpconfig.h"
#include <sddf/serial/queue.h>
#include "py/stream.h"

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    char c;

    // Wait for a notification from the RX multiplexer if we do not have
    // any data to process.

    // This is in a loop because the notification for a particular
    // buffer may only be delivered after we have already consumed it.
    while(serial_queue_empty(&serial_rx_queue_handle,
                             serial_rx_queue_handle.queue->head)) {
        microkit_cothread_wait_on_channel(SERIAL_RX_CH);
    }

    // Dequeue buffer and return char
    int ret = serial_dequeue(&serial_rx_queue_handle,
                             &serial_rx_queue_handle.queue->head,
                             &c);
    assert(!ret);

    serial_request_producer_signal(&serial_rx_queue_handle);

    return c;
}


// Send the string of given length.
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len)
{
    for (;;) {
        uint32_t n = serial_enqueue_batch(&serial_tx_queue_handle, len, str);
        if (n != 0 && serial_require_producer_signal(&serial_tx_queue_handle)) {
            serial_cancel_producer_signal(&serial_tx_queue_handle);
            microkit_notify(SERIAL_TX_CH);
        }
        len -= n;
        if (len == 0) {
            break;
        }

        serial_request_consumer_signal(&serial_tx_queue_handle);
        if (serial_queue_full(&serial_tx_queue_handle, serial_tx_queue_handle.queue->tail)) {
            microkit_cothread_wait_on_channel(SERIAL_TX_CH);
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
    if ((poll_flags & MP_STREAM_POLL_WR) && !serial_queue_full(&serial_tx_queue_handle, serial_tx_queue_handle.queue->head)) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}
