#include <microkit.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include "micropython.h"
#include "py/mpconfig.h"
#include <sddf/serial/queue.h>

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    char c;

    // Wait for a notification from the RX multiplexer if we do not have
    // any data to process.

    // This is in a loop because the notification for a particular
    // buffer may only be delivered after we have already consumed it.
    while(serial_queue_empty(&serial_rx_queue_handle, serial_rx_queue_handle.queue->head)) {
        microkit_cothread_wait_on_channel(SERIAL_RX_CH);
    }

    // Dequeue buffer and return char
        
    int ret = serial_dequeue(&serial_rx_queue_handle,  &serial_rx_queue_handle.queue->head,
                             &c);
    assert(!ret);

    serial_request_producer_signal(&serial_rx_queue_handle);

    return c;
}


// Send the string of given length.
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    uint32_t n;
    bool transferred = false;

    do {
        n = serial_enqueue_batch(&serial_tx_queue_handle, len, str);
        transferred |= n;
        len -= n;
    } while (n && len);

    if (transferred && serial_require_producer_signal(&serial_tx_queue_handle)) {
        serial_cancel_producer_signal(&serial_tx_queue_handle);
        microkit_notify(SERIAL_TX_CH);
    }
}
