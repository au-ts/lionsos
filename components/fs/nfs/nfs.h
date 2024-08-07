/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <sddf/serial/queue.h>

#define SERIAL_TX_CH 0
#define ETHERNET_RX_CHANNEL 2
#define ETHERNET_TX_CHANNEL 3
#define CLIENT_CHANNEL 7
#define TIMER_CHANNEL 9

extern serial_queue_handle_t serial_tx_queue_handle;

extern struct nfs_context *nfs;

void continuation_pool_init(void);
void process_commands(void);

int must_notify_rx(void);
int must_notify_tx(void);
