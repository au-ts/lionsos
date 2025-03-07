/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once
#ifndef FILEIO_ETHERNET_CONFIG_H
#define FILEIO_ETHERNET_CONFIG_H

#include <microkit.h>
#include <sddf/network/queue.h>
#include <sddf/util/string.h>

#define NUM_NETWORK_CLIENTS 1

#define NET_CLI0_NAME "micropython"
#define NET_COPY0_NAME "eth_copy_mp"
#define NET_VIRT_RX_NAME "eth_virt_rx"
#define NET_VIRT_TX_NAME "eth_virt_tx"
#define NET_DRIVER_NAME "eth"

#define NET_DATA_REGION_SIZE                    0x200000
#define NET_HW_REGION_SIZE                      0x10000

#if defined(CONFIG_PLAT_ODROIDC4)
#define MAC_ADDR_CLI0                       0x525401000003
#elif defined(CONFIG_PLAT_MAAXBOARD)
#define MAC_ADDR_CLI0                       0x525401000005
#elif defined(CONFIG_PLAT_QEMU_ARM_VIRT)
#define MAC_ADDR_CLI0                       0x525401000007
#else
#error "Must define MAC addresses for clients in ethernet config"
#endif

#define NET_TX_QUEUE_CAPACITY_CLI0                   512
#define NET_TX_QUEUE_CAPACITY_DRIV                   (NET_TX_QUEUE_CAPACITY_CLI0)

#define NET_TX_DATA_REGION_SIZE_CLI0                 NET_DATA_REGION_SIZE

_Static_assert(NET_TX_DATA_REGION_SIZE_CLI0 >=
               NET_TX_QUEUE_CAPACITY_CLI0 * NET_BUFFER_SIZE,
               "Client0 TX data region size must fit Client0 TX buffers");

#define NET_RX_QUEUE_CAPACITY_DRIV                  512
#define NET_RX_QUEUE_CAPACITY_CLI0                  512
#define NET_RX_QUEUE_CAPACITY_COPY0                 NET_RX_QUEUE_CAPACITY_DRIV

#define NET_RX_DATA_REGION_SIZE_DRIV            NET_DATA_REGION_SIZE
#define NET_RX_DATA_REGION_SIZE_CLI0            NET_DATA_REGION_SIZE

_Static_assert(NET_RX_DATA_REGION_SIZE_DRIV >=
               NET_RX_QUEUE_CAPACITY_DRIV * NET_BUFFER_SIZE,
               "Driver RX data region size must fit Driver RX buffers");
_Static_assert(NET_RX_DATA_REGION_SIZE_CLI0 >=
               NET_RX_QUEUE_CAPACITY_CLI0 * NET_BUFFER_SIZE,
               "Client0 RX data region size must fit Client0 RX buffers");

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#define NET_MAX_QUEUE_CAPACITY MAX(NET_TX_QUEUE_CAPACITY_DRIV, \
                               MAX(NET_RX_QUEUE_CAPACITY_DRIV, \
                                   NET_RX_QUEUE_CAPACITY_CLI0))

_Static_assert(NET_TX_QUEUE_CAPACITY_DRIV >= NET_TX_QUEUE_CAPACITY_CLI0,
               "Network Driver TX queue must have capacity to fit "
               "all of client's TX buffers.");
_Static_assert(NET_RX_QUEUE_CAPACITY_COPY0 >= NET_RX_QUEUE_CAPACITY_DRIV,
               "Network Copy0 queue must have capacity to fit all RX buffers.");
_Static_assert(sizeof(net_queue_t) + NET_MAX_QUEUE_CAPACITY * sizeof(net_buff_desc_t)
               <= NET_DATA_REGION_SIZE,
               "net_queue_t must fit into a single data region.");

static inline uint64_t net_cli_mac_addr(char *pd_name)
{
    if (!sddf_strcmp(pd_name, NET_CLI0_NAME)) {
        return MAC_ADDR_CLI0;
    }

    return 0;
}

static inline void net_virt_mac_addrs(char *pd_name, uint64_t macs[NUM_NETWORK_CLIENTS])
{
    if (!sddf_strcmp(pd_name, NET_VIRT_RX_NAME)) {
        macs[0] = MAC_ADDR_CLI0;
    }
}

static inline void net_cli_queue_capacity(char *pd_name, size_t *rx_QUEUE_CAPACITY, size_t *tx_QUEUE_CAPACITY)
{
    if (!sddf_strcmp(pd_name, NET_CLI0_NAME)) {
        *rx_QUEUE_CAPACITY = NET_RX_QUEUE_CAPACITY_CLI0;
        *tx_QUEUE_CAPACITY = NET_TX_QUEUE_CAPACITY_CLI0;
    }
}

static inline void net_copy_queue_capacity(char *pd_name, size_t *cli_QUEUE_CAPACITY, size_t *virt_QUEUE_CAPACITY)
{
    if (!sddf_strcmp(pd_name, NET_COPY0_NAME)) {
        *cli_QUEUE_CAPACITY = NET_RX_QUEUE_CAPACITY_CLI0;
        *virt_QUEUE_CAPACITY = NET_RX_QUEUE_CAPACITY_COPY0;
    }
}

typedef struct net_queue_info {
    net_queue_t *free;
    net_queue_t *active;
    size_t capacity;
} net_queue_info_t;

static inline void net_virt_queue_info(char *pd_name, net_queue_t *cli0_free, net_queue_t *cli0_active,
                                       net_queue_info_t ret[NUM_NETWORK_CLIENTS])
{
    if (!sddf_strcmp(pd_name, NET_VIRT_RX_NAME)) {
        ret[0] = (net_queue_info_t) {
            .free = cli0_free, .active = cli0_active, .capacity = NET_RX_QUEUE_CAPACITY_COPY0
        };
    } else if (!sddf_strcmp(pd_name, NET_VIRT_TX_NAME)) {
        ret[0] = (net_queue_info_t) {
            .free = cli0_free, .active = cli0_active, .capacity = NET_TX_QUEUE_CAPACITY_CLI0
        };
    }
}

static inline void net_mem_region_vaddr(char *pd_name, uintptr_t mem_regions[NUM_NETWORK_CLIENTS],
                                        uintptr_t start_region)
{
    if (!sddf_strcmp(pd_name, NET_VIRT_TX_NAME)) {
        mem_regions[0] = start_region;
    }
}

// Hacking define to get micropython's mpnetworkport.c compiled
#define NET_RX_QUEUE_CAPACITY_CLI1               512

#endif /* FILEIO_ETHERNET_CONFIG_H */
