#pragma once

#include <sddf/network/queue.h>

#define NUM_NETWORK_CLIENTS 1

#define CLI0_NAME "micropython"
#define COPY0_NAME "eth_copy_mp"
#define VIRT_RX_NAME "eth_virt_rx"
#define VIRT_TX_NAME "eth_virt_tx"
#define DRIVER_NAME "eth"

#define DATA_REGION_SIZE                    0x200000
#define HW_REGION_SIZE                      0x10000

#define MAC_ADDR_CLI0                       0x525401000010

#define TX_QUEUE_SIZE_CLI0                   512
#define TX_QUEUE_SIZE_DRIV                   (TX_QUEUE_SIZE_CLI0)

#define TX_DATA_REGION_SIZE_CLI0            DATA_REGION_SIZE

_Static_assert(TX_DATA_REGION_SIZE_CLI0 >= TX_QUEUE_SIZE_CLI0 * NET_BUFFER_SIZE, "Client0 TX data region size must fit Client0 TX buffers");

#define RX_QUEUE_SIZE_DRIV                   512
#define RX_QUEUE_SIZE_CLI0                   512
// Hacking define to get micropython's mpnetworkport.c compiled
#define RX_QUEUE_SIZE_CLI1                   512
#define RX_QUEUE_SIZE_COPY0                  RX_QUEUE_SIZE_DRIV

#define RX_DATA_REGION_SIZE_DRIV            DATA_REGION_SIZE
#define RX_DATA_REGION_SIZE_CLI0            DATA_REGION_SIZE

_Static_assert(RX_DATA_REGION_SIZE_DRIV >= RX_QUEUE_SIZE_DRIV * NET_BUFFER_SIZE, "Driver RX data region size must fit Driver RX buffers");
_Static_assert(RX_DATA_REGION_SIZE_CLI0 >= RX_QUEUE_SIZE_CLI0 * NET_BUFFER_SIZE, "Client0 RX data region size must fit Client0 RX buffers");

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#define ETH_MAX_QUEUE_SIZE MAX(TX_QUEUE_SIZE_DRIV, MAX(RX_QUEUE_SIZE_DRIV, MAX(RX_QUEUE_SIZE_CLI0, RX_QUEUE_SIZE_CLI1)))
_Static_assert(TX_QUEUE_SIZE_DRIV >= TX_QUEUE_SIZE_CLI0, "Driver TX queue must have capacity to fit all of client's TX buffers.");
_Static_assert(RX_QUEUE_SIZE_COPY0 >= RX_QUEUE_SIZE_DRIV, "Copy0 queue must have capacity to fit all RX buffers.");
_Static_assert(sizeof(net_queue_t) <= DATA_REGION_SIZE, "Queue must fit into a single data region.");

static bool __str_match(const char *s0, const char *s1)
{
    while (*s0 != '\0' && *s1 != '\0' && *s0 == *s1) {
        s0++, s1++;
    }
    return *s0 == *s1;
}

static void __set_mac_addr(uint8_t *mac, uint64_t val)
{
    mac[0] = val >> 40 & 0xff;
    mac[1] = val >> 32 & 0xff;
    mac[2] = val >> 24 & 0xff;
    mac[3] = val >> 16 & 0xff;
    mac[4] = val >> 8 & 0xff;
    mac[5] = val & 0xff;
}

static inline void cli_mac_addr_init_sys(char *pd_name, uint8_t *macs)
{
    if (__str_match(pd_name, CLI0_NAME)) {
        __set_mac_addr(macs, MAC_ADDR_CLI0);
    }
}

static inline void virt_mac_addr_init_sys(char *pd_name, uint8_t *macs)
{
    if (__str_match(pd_name, VIRT_RX_NAME)) {
        __set_mac_addr(macs, MAC_ADDR_CLI0);
    }
}

static inline void cli_queue_init_sys(char *pd_name, net_queue_handle_t *rx_queue, uintptr_t rx_free,
                                      uintptr_t rx_active,
                                      net_queue_handle_t *tx_queue, uintptr_t tx_free, uintptr_t tx_active)
{
    if (__str_match(pd_name, CLI0_NAME)) {
        net_queue_init(rx_queue, (net_queue_t *) rx_free, (net_queue_t *) rx_active, RX_QUEUE_SIZE_CLI0);
        net_queue_init(tx_queue, (net_queue_t *) tx_free, (net_queue_t *) tx_active, TX_QUEUE_SIZE_CLI0);
    }
}

static inline void copy_queue_init_sys(char *pd_name, net_queue_handle_t *cli_queue, uintptr_t cli_free,
                                       uintptr_t cli_active,
                                       net_queue_handle_t *virt_queue, uintptr_t virt_free, uintptr_t virt_active)
{
    if (__str_match(pd_name, COPY0_NAME)) {
        net_queue_init(cli_queue, (net_queue_t *) cli_free, (net_queue_t *) cli_active, RX_QUEUE_SIZE_CLI0);
        net_queue_init(virt_queue, (net_queue_t *) virt_free, (net_queue_t *) virt_active, RX_QUEUE_SIZE_COPY0);
    }
}

static inline void virt_queue_init_sys(char *pd_name, net_queue_handle_t *cli_queue, uintptr_t cli_free,
                                       uintptr_t cli_active)
{
    if (__str_match(pd_name, VIRT_RX_NAME)) {
        net_queue_init(cli_queue, (net_queue_t *) cli_free, (net_queue_t *) cli_active, RX_QUEUE_SIZE_COPY0);
    } else if (__str_match(pd_name, VIRT_TX_NAME)) {
        net_queue_init(cli_queue, (net_queue_t *) cli_free, (net_queue_t *) cli_active, TX_QUEUE_SIZE_CLI0);
    }
}

static inline void mem_region_init_sys(char *pd_name, uintptr_t *mem_regions, uintptr_t start_region)
{
    if (__str_match(pd_name, VIRT_TX_NAME)) {
        mem_regions[0] = start_region;
    }
}