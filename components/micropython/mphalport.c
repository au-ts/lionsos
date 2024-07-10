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
    // Wait for a notification from the RX multiplexer if we do not have
    // any data to process.

    // This is in a loop because the notification for a particular
    // buffer may only be delivered after we have already consumed it.
    while(serial_queue_empty(serial_rx_queue.active)) {
        microkit_cothread_wait_on_channel(SERIAL_RX_CH);
    }

    // Dequeue buffer and return char
    uintptr_t buffer = 0;
    unsigned int buffer_len = 0;
    int ret = serial_dequeue_active(&serial_rx_queue, &buffer, &buffer_len);
    if (ret) {
        microkit_dbg_puts("MP|ERROR: could not dequeue serial RX used buffer\n");
        return 0;
    }

    char ch = ((char *)buffer)[0];

    ret = serial_enqueue_free(&serial_rx_queue, buffer, BUFFER_SIZE);
    if (ret) {
        microkit_dbg_puts("MP|ERROR: could not enqueue serial RX free buffer\n");
        return 0;
    }

    return ch;
}

// Send the string of given length.
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    uintptr_t buffer = 0;
    unsigned int buffer_len = 0;
    int ret = serial_dequeue_free(&serial_tx_queue, &buffer, &buffer_len);
    if (ret) {
        microkit_dbg_puts("MP|ERROR: could not dequeue serial TX free buffer\n");
        return;
    }

    // @ivanv, fix
    if (buffer_len < len) {
        microkit_dbg_puts("MP|ERROR: todo, handle large buffers in serial");
        return;
    }

    // @ivanv: use memcpy instead? Wait for libc integration first
    char *str_buf = (char *) buffer;
    for (int i = 0; i < len; i++) {
        str_buf[i] = str[i];
    }

    ret = serial_enqueue_active(&serial_tx_queue, buffer, len);
    // @ivanv: this error condition is a real possibilily and should be handled properly
    if (ret) {
        microkit_dbg_puts("MP|ERROR: could not enqueue active serial TX buffer\n");
    }

    microkit_notify(SERIAL_TX_CH);
}
