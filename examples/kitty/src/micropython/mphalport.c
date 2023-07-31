#include <sel4cp.h>
#include <unistd.h>
#include "py/mpconfig.h"
#include "uart.h"

// @ivanv: TEMPORARY until the meson serial driver is in sDDF

// Receive single character, blocking until one is available.
int mp_hal_stdin_rx_chr(void) {
    return uart_get_char();
}

// Send the string of given length.
void mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    for (int i = 0; i < len; i++) {
        uart_put_char(str[i]);
    }
}
