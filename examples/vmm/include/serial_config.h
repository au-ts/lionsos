/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>
#include <sddf/util/string.h>
#include <sddf/serial/queue.h>
#include <stdint.h>

/* Number of clients */
#define SERIAL_NUM_CLIENTS 1

/* Only support transmission and not receive. */
#define SERIAL_TX_ONLY 0

/* Associate a colour with each client's output. */
#define SERIAL_WITH_COLOUR 0

/*
 * Control character to switch input stream - ctrl \.
 * To input character input twice.
 */
#define SERIAL_SWITCH_CHAR 28

/* Control character to terminate client number input. */
#define SERIAL_TERMINATE_NUM '\r'

/* Default baud rate of the uart device */
#define UART_DEFAULT_BAUD 115200

/* String to be printed to start console input */
#define SERIAL_CONSOLE_BEGIN_STRING "Begin input\n"
#define SERIAL_CONSOLE_BEGIN_STRING_LEN 13

#define SERIAL_CLI0_NAME "CLIENT_VM"
#define SERIAL_VIRT_RX_NAME "serial_virt_rx"
#define SERIAL_VIRT_TX_NAME "serial_virt_tx"
#define SERIAL_DRIVER_NAME "uart_driver"

#define SERIAL_QUEUE_SIZE                          0x1000
#define SERIAL_DATA_REGION_SIZE                    0x2000

#define SERIAL_TX_DATA_REGION_SIZE_DRIV            SERIAL_DATA_REGION_SIZE
#define SERIAL_TX_DATA_REGION_SIZE_CLI0            SERIAL_DATA_REGION_SIZE

#define SERIAL_RX_DATA_REGION_SIZE_DRIV            SERIAL_DATA_REGION_SIZE
#define SERIAL_RX_DATA_REGION_SIZE_CLI0            SERIAL_DATA_REGION_SIZE

#define SERIAL_MAX_TX_DATA_SIZE MAX(        \
        SERIAL_TX_DATA_REGION_SIZE_DRIV,    \
        SERIAL_TX_DATA_REGION_SIZE_CLI0 \
        )
#define SERIAL_MAX_RX_DATA_SIZE MAX(     \
        SERIAL_RX_DATA_REGION_SIZE_DRIV, \
        SERIAL_RX_DATA_REGION_SIZE_CLI0  \
        )
#define SERIAL_MAX_DATA_SIZE MAX( \
        SERIAL_MAX_TX_DATA_SIZE,  \
        SERIAL_MAX_RX_DATA_SIZE   \
        )
_Static_assert(SERIAL_MAX_DATA_SIZE < UINT32_MAX,
               "Data regions must be smaller than UINT32 max"
               " to use the queue data structure correctly.");

static inline void serial_cli_queue_init_sys(
    char *pd_name, serial_queue_handle_t *rx_queue_handle,
    serial_queue_t *rx_queue, char *rx_data,
    serial_queue_handle_t *tx_queue_handle,
    serial_queue_t *tx_queue, char *tx_data)
{
    if (!sddf_strcmp(pd_name, SERIAL_CLI0_NAME)) {
        serial_queue_init(rx_queue_handle, rx_queue, SERIAL_RX_DATA_REGION_SIZE_CLI0, rx_data);
        serial_queue_init(tx_queue_handle, tx_queue, SERIAL_TX_DATA_REGION_SIZE_CLI0, tx_data);
    }
}

static inline void serial_virt_queue_init_sys(
    char *pd_name, serial_queue_handle_t *cli_queue_handle,
    serial_queue_t *cli_queue, char *cli_data)
{
    if (!sddf_strcmp(pd_name, SERIAL_VIRT_RX_NAME)) {
        serial_queue_init(cli_queue_handle,
                          cli_queue,
                          SERIAL_RX_DATA_REGION_SIZE_CLI0,
                          cli_data);
    } else if (!sddf_strcmp(pd_name, SERIAL_VIRT_TX_NAME)) {
        serial_queue_init(cli_queue_handle,
                          cli_queue,
                          SERIAL_TX_DATA_REGION_SIZE_CLI0,
                          cli_data);
    }
}

#if SERIAL_WITH_COLOUR
static inline void serial_channel_names_init(char **client_names)
{
    client_names[0] = SERIAL_CLI0_NAME;
}
#endif
