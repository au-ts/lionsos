#include <microkit.h>
#include <unistd.h>
#include "micropython.h"
#include "py/mpconfig.h"
#include <sddf/serial/shared_ringbuffer.h>

extern ring_handle_t serial_rx_ring;
extern ring_handle_t serial_tx_ring;

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    // Wait for notification from RX multiplexor.
    mp_blocking_events = mp_event_source_serial;
    co_switch(t_event);
    mp_blocking_events = mp_event_source_none;
    // Dequeue buffer and return char
    uintptr_t buffer = 0;
    unsigned int buffer_len = 0;
    void *cookie = 0;
    int ret = dequeue_used(&serial_rx_ring, &buffer, &buffer_len, &cookie);
    if (ret) {
        microkit_dbg_puts("MP|ERROR: could not dequeue serial RX used buffer\n");
        return 0;
    }

    char ch = ((char *)buffer)[0];

    ret = enqueue_free(&serial_rx_ring, buffer, BUFFER_SIZE, cookie);
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
    void *cookie = 0;
    int ret = dequeue_free(&serial_tx_ring, &buffer, &buffer_len, &cookie);
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

    ret = enqueue_used(&serial_tx_ring, buffer, len, cookie);
    // @ivanv: this error condition is a real possibilily and should be handled properly
    if (ret) {
        microkit_dbg_puts("MP|ERROR: could not enqueue used serial TX buffer\n");
    }

    microkit_notify(SERIAL_TX_CH);
}
