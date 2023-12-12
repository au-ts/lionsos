/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define ARP_CHANNEL 7


extern struct nfs_context *nfs;

void continuation_pool_init(void);
void nfs_notified(void);

int must_notify_rx(void);
int must_notify_tx(void);
