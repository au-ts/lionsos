#include <microkit.h>
#include <unistd.h>
#include <assert.h>
#include "micropython.h"
#include "py/mpconfig.h"
#include <sddf/serial/queue.h>

extern serial_queue_handle_t serial_rx_queue;
extern serial_queue_handle_t serial_tx_queue;

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    char c;

    // Wait for a notification from the RX multiplexer if we do not have
    // any data to process.

    // This is in a loop because the notification for a particular
    // buffer may only be delivered after we have already consumed it.
    while(serial_queue_empty(&serial_rx_queue, serial_rx_queue.queue->head)) {
        microkit_cothread_wait_on_channel(SERIAL_RX_CH);
    }

    // Dequeue buffer and return char
        
    int ret = serial_dequeue(&serial_rx_queue,  &serial_rx_queue.queue->head,
                             &c);
    if (ret) {
        microkit_dbg_puts("MP|ERROR: could not dequeue serial RX used buffer\n");
        return 0;
    }
    return c;
}


// Send the string of given length.
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    int n;

    do {
        n = serial_enqueue_batch(&serial_tx_queue, len, str);
        len -= n;
    } while (n && len);

    if (len) {
        microkit_dbg_puts("MP|ERROR: could not enqueue active serial TX buffer\n");
    }

    microkit_notify(SERIAL_TX_CH);
}
