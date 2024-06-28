#include <microkit.h>
#include <unistd.h>
#include <assert.h>
#include "micropython.h"
#include "py/mpconfig.h"
#include <sddf/serial/queue.h>
#include "py/stream.h"
#include "py/ringbuf.h"
#include "shared/runtime/interrupt_char.h"

extern serial_queue_handle_t serial_rx_queue;
extern serial_queue_handle_t serial_tx_queue;

/* This is the internal micropython ringbuffer we will copy into from our sDDF
ringbuffers. */
STATIC uint8_t stdin_ringbuf_array[NUM_ENTRIES];
ringbuf_t stdin_ringbuf = { stdin_ringbuf_array, sizeof(stdin_ringbuf_array) };

bool process_sddf_rx_chr(void) {
    /* Check if our stdin_ringbuf is full. If it is, then we will
    let the sDDF queues buffer our input. */
    while (!serial_queue_empty(serial_rx_queue.active) && ringbuf_free(&stdin_ringbuf) != 0) {
        // Dequeue buffer and return char
        uintptr_t buffer = 0;
        unsigned int buffer_len = 0;
        int ret = serial_dequeue_active(&serial_rx_queue, &buffer, &buffer_len);
        assert(ret == 0);

        char ch = ((char *)buffer)[0];

        ret = serial_enqueue_free(&serial_rx_queue, buffer, BUFFER_SIZE);
        assert(ret == 0);

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
        await(mp_event_source_serial);
    }

    c = ringbuf_get(&stdin_ringbuf);

    /* Process the sDDF queues again, in the case that we have buffered our input
    in them. */
    process_sddf_rx_chr();

    return c;
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

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if ((poll_flags & MP_STREAM_POLL_RD) && ringbuf_peek(&stdin_ringbuf)) {
        ret |= MP_STREAM_POLL_RD;
    }
    if ((poll_flags & MP_STREAM_POLL_WR) && !serial_queue_empty(serial_tx_queue.free)) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}
