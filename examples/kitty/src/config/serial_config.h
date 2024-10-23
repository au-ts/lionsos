/*
 * Configuration for serial subsystems in Kitty system
 *
 *  Copyright 2024 UNSW
 *  SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef KITTY_SERIAL_CONFIG_H
#define KITTY_SERIAL_CONFIG_H
#pragma once

/* Number of clients of the serial subsystem. */
#define SERIAL_NUM_CLIENTS 4

/* Support full duplex. */
#define SERIAL_TX_ONLY 0

/* Associate a colour with each client's output. */
#define SERIAL_WITH_COLOUR 1

/* Default baud rate of the uart device */
#define UART_DEFAULT_BAUD 115200

/* One read/write client, one write only client */
#define SERIAL_CLI0_NAME "micropython"
#define SERIAL_CLI1_NAME "nfs"
#define SERIAL_CLI2_NAME "framebuffer_vmm"
#define SERIAL_CLI3_NAME "ethernet_vmm"
#define SERIAL_VIRT_RX_NAME "serial_virt_rx"
#define SERIAL_VIRT_TX_NAME "serial_virt_tx"

#define SERIAL_QUEUE_CAPACITY                          0x1000
#define SERIAL_DATA_REGION_CAPACITY                    0x2000

#define SERIAL_TX_DATA_REGION_CAPACITY_DRIV            (2 * SERIAL_DATA_REGION_CAPACITY)
#define SERIAL_TX_DATA_REGION_CAPACITY_CLI0            SERIAL_DATA_REGION_CAPACITY
#define SERIAL_TX_DATA_REGION_CAPACITY_CLI1            SERIAL_DATA_REGION_CAPACITY

#define SERIAL_RX_DATA_REGION_CAPACITY_DRIV            SERIAL_DATA_REGION_CAPACITY
#define SERIAL_RX_DATA_REGION_CAPACITY_CLI0            SERIAL_DATA_REGION_CAPACITY

#define SERIAL_MAX_TX_DATA_CAPACITY MAX(SERIAL_TX_DATA_REGION_CAPACITY_DRIV, \
                                    MAX(SERIAL_TX_DATA_REGION_CAPACITY_CLI0, \
                                    SERIAL_TX_DATA_REGION_CAPACITY_CLI1))
#define SERIAL_MAX_RX_DATA_CAPACITY MAX(SERIAL_RX_DATA_REGION_CAPACITY_DRIV, \
                                    SERIAL_RX_DATA_REGION_CAPACITY_CLI0)
#define SERIAL_MAX_DATA_CAPACITY MAX(SERIAL_MAX_TX_DATA_CAPACITY, \
                                 SERIAL_MAX_RX_DATA_CAPACITY)

/* String to be printed to start console input */
#define SERIAL_CONSOLE_BEGIN_STRING ""
#define SERIAL_CONSOLE_BEGIN_STRING_LEN 0

_Static_assert(SERIAL_MAX_DATA_CAPACITY < UINT32_MAX,
               "Data regions must be smaller than UINT32"
               " max to use queue data structure correctly.");

static inline void serial_cli_queue_init_sys(const char *pd_name,
                                             serial_queue_handle_t *rx_queue_handle,
                                             serial_queue_t *rx_queue,
                                             char *rx_data,
                                             serial_queue_handle_t *tx_queue_handle,
                                             serial_queue_t *tx_queue,
                                             char *tx_data)
{
    if (!sddf_strcmp(pd_name, SERIAL_CLI0_NAME)) {
        serial_queue_init(rx_queue_handle, rx_queue,
                        SERIAL_RX_DATA_REGION_CAPACITY_CLI0, rx_data);
        serial_queue_init(tx_queue_handle, tx_queue,
                        SERIAL_TX_DATA_REGION_CAPACITY_CLI0, tx_data);
    } else if (!sddf_strcmp(pd_name, SERIAL_CLI1_NAME)) {
        serial_queue_init(tx_queue_handle, tx_queue,
                        SERIAL_TX_DATA_REGION_CAPACITY_CLI1, tx_data);
    } else if (!sddf_strcmp(pd_name, SERIAL_CLI2_NAME)) {
        serial_queue_init(rx_queue_handle, rx_queue,
                        SERIAL_RX_DATA_REGION_CAPACITY_CLI0, rx_data);
        serial_queue_init(tx_queue_handle, tx_queue,
                        SERIAL_TX_DATA_REGION_CAPACITY_CLI0, tx_data);
    } else if (!sddf_strcmp(pd_name, SERIAL_CLI3_NAME)) {
        serial_queue_init(rx_queue_handle, rx_queue,
                        SERIAL_RX_DATA_REGION_CAPACITY_CLI0, rx_data);
        serial_queue_init(tx_queue_handle, tx_queue,
                        SERIAL_TX_DATA_REGION_CAPACITY_CLI0, tx_data);
    }
}

static inline void serial_virt_queue_init_sys(char *pd_name,
                                              serial_queue_handle_t *cli_queue_handle,
                                              serial_queue_t *cli_queue,
                                              char *cli_data)
{
    if (!sddf_strcmp(pd_name, SERIAL_VIRT_RX_NAME)) {
        serial_queue_init(cli_queue_handle, cli_queue,
                          SERIAL_RX_DATA_REGION_CAPACITY_CLI0, cli_data);
        serial_queue_init(&cli_queue_handle[2], (serial_queue_t *)(cli_queue + 2 * SERIAL_QUEUE_CAPACITY),
                          SERIAL_RX_DATA_REGION_CAPACITY_CLI0,
                          (char *)(cli_data + SERIAL_RX_DATA_REGION_CAPACITY_CLI0 + SERIAL_RX_DATA_REGION_CAPACITY_CLI0));
        serial_queue_init(&cli_queue_handle[3], (serial_queue_t *)(cli_queue + 3 * SERIAL_QUEUE_CAPACITY),
                          SERIAL_RX_DATA_REGION_CAPACITY_CLI0,
                          (char *)(cli_data + SERIAL_RX_DATA_REGION_CAPACITY_CLI0 + SERIAL_RX_DATA_REGION_CAPACITY_CLI0));
    } else if (!sddf_strcmp(pd_name, SERIAL_VIRT_TX_NAME)) {
        serial_queue_init(cli_queue_handle, cli_queue,
                          SERIAL_TX_DATA_REGION_CAPACITY_CLI0, cli_data);
        serial_queue_init(&cli_queue_handle[1],
                          (serial_queue_t *)((uintptr_t)cli_queue +
                                             SERIAL_QUEUE_CAPACITY),
                          SERIAL_TX_DATA_REGION_CAPACITY_CLI1,
                          cli_data + SERIAL_TX_DATA_REGION_CAPACITY_CLI0);
        serial_queue_init(&cli_queue_handle[2],
                          (serial_queue_t *)((uintptr_t)cli_queue +
                                             SERIAL_QUEUE_CAPACITY * 2),
                          SERIAL_TX_DATA_REGION_CAPACITY_CLI1,
                          cli_data + SERIAL_TX_DATA_REGION_CAPACITY_CLI0);
        serial_queue_init(&cli_queue_handle[3],
                          (serial_queue_t *)((uintptr_t)cli_queue +
                                             SERIAL_QUEUE_CAPACITY * 3),
                          SERIAL_TX_DATA_REGION_CAPACITY_CLI1,
                          cli_data + SERIAL_TX_DATA_REGION_CAPACITY_CLI0);
    }
}

#if SERIAL_WITH_COLOUR
static inline void serial_channel_names_init(char **client_names)
{
    client_names[0] = SERIAL_CLI0_NAME;
    client_names[1] = SERIAL_CLI1_NAME;
    client_names[2] = SERIAL_CLI2_NAME;
    client_names[3] = SERIAL_CLI3_NAME;
}
#endif

/*
 * UNUSED but needed for compilation
 */
#define SERIAL_SWITCH_CHAR '\0'
#define SERIAL_TERMINATE_NUM (4) /* control-D */

#endif /* KITTY_SERIAL_CONFIG_H */
