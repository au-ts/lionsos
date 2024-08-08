/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <sddf/serial/queue.h>

#define SERIAL_TX_CH 0
extern serial_queue_handle_t serial_tx_queue_handle;

extern struct nfs_context *nfs;

void continuation_pool_init(void);
void process_commands(void);

int must_notify_rx(void);
int must_notify_tx(void);
