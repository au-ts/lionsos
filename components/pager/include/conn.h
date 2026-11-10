/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _CONN_H
#define _CONN_H

#include <stdbool.h>
#include <stdint.h>
#include <sel4/sel4.h>


void conn_init(void);
int conn_connect(uint32_t pid, uint8_t template_client, seL4_CPtr cspace, seL4_CPtr ntfn_root,
                 seL4_Word ntfn_slot);
void conn_release(uint32_t pid);
bool conn_mac_addr(uint32_t pid, uint8_t mac_addr[6]);

#endif
