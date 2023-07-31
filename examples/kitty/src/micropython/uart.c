#include <stdint.h>
#include "uart.h"

#define UART_WFIFO 0x0
#define UART_RFIFO 0x4
#define UART_STATUS 0xC

#define UART_TX_FULL (1 << 21)
#define UART_RX_EMPTY (1 << 20)

uintptr_t uart_base;

#define REG_PTR(offset) ((volatile uint32_t *)((uart_base) + (offset)))

int uart_get_char()
{
    while ((*REG_PTR(UART_STATUS) & UART_RX_EMPTY));

    return *REG_PTR(UART_RFIFO);
}

int uart_put_char(int c)
{
    while ((*REG_PTR(UART_STATUS) & UART_TX_FULL));

    /* Add character to the buffer. */
    *REG_PTR(UART_WFIFO) = c & 0x7f;
    if (c == '\n') {
        uart_put_char('\r');
    }

    return c;
}
