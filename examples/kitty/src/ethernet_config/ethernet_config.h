#pragma once

#include <sddf/network/queue.h>

#define NUM_CLIENTS 3

#define ARP_NAME "arp"
#define CLI0_NAME "nfs"
#define CLI1_NAME "micropython"
#define COPY0_NAME "eth_copy_nfs"
#define COPY1_NAME "eth_copy_mp"
#define VIRT_RX_NAME "eth_virt_rx"
#define VIRT_TX_NAME "eth_virt_tx"
#define DRIVER_NAME "eth"

#define DATA_REGION_SIZE                    0x200000
#define HW_REGION_SIZE                      0x10000

#define MAC_ADDR_ARP                        0xFFFFFFFFFFFF
#define MAC_ADDR_CLI0                       0x525401000010
#define MAC_ADDR_CLI1                       0x525401000011

#define TX_QUEUE_SIZE_ARP                    512
#define TX_QUEUE_SIZE_CLI0                   512
#define TX_QUEUE_SIZE_CLI1                   512
#define TX_QUEUE_SIZE_DRIV                   (TX_QUEUE_SIZE_ARP + TX_QUEUE_SIZE_CLI0 + TX_QUEUE_SIZE_CLI1)

#define TX_DATA_REGION_SIZE_ARP             DATA_REGION_SIZE
#define TX_DATA_REGION_SIZE_CLI0            DATA_REGION_SIZE
#define TX_DATA_REGION_SIZE_CLI1            DATA_REGION_SIZE

_Static_assert(TX_DATA_REGION_SIZE_ARP >= TX_QUEUE_SIZE_ARP * NET_BUFFER_SIZE, "Arp TX data region size must fit Arp TX buffers");
_Static_assert(TX_DATA_REGION_SIZE_CLI0 >= TX_QUEUE_SIZE_CLI0 * NET_BUFFER_SIZE, "Client0 TX data region size must fit Client0 TX buffers");
_Static_assert(TX_DATA_REGION_SIZE_CLI1 >= TX_QUEUE_SIZE_CLI1 * NET_BUFFER_SIZE, "Client1 TX data region size must fit Client1 TX buffers");

#define RX_QUEUE_SIZE_DRIV                   512
#define RX_QUEUE_SIZE_ARP                    RX_QUEUE_SIZE_DRIV
#define RX_QUEUE_SIZE_CLI0                   512
#define RX_QUEUE_SIZE_CLI1                   512
#define RX_QUEUE_SIZE_COPY0                  RX_QUEUE_SIZE_DRIV
#define RX_QUEUE_SIZE_COPY1                  RX_QUEUE_SIZE_DRIV

#define RX_DATA_REGION_SIZE_DRIV            DATA_REGION_SIZE
#define RX_DATA_REGION_SIZE_CLI0            DATA_REGION_SIZE
#define RX_DATA_REGION_SIZE_CLI1            DATA_REGION_SIZE

_Static_assert(RX_DATA_REGION_SIZE_DRIV >= RX_QUEUE_SIZE_DRIV * NET_BUFFER_SIZE, "Driver RX data region size must fit Driver RX buffers");
_Static_assert(RX_DATA_REGION_SIZE_CLI0 >= RX_QUEUE_SIZE_CLI0 * NET_BUFFER_SIZE, "Client0 RX data region size must fit Client0 RX buffers");
_Static_assert(RX_DATA_REGION_SIZE_CLI1 >= RX_QUEUE_SIZE_CLI1 * NET_BUFFER_SIZE, "Client1 RX data region size must fit Client1 RX buffers");

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

_Static_assert(NET_MAX_BUFFERS >= TX_QUEUE_SIZE_DRIV, "Queue capacity must be >= largest TX queue.");
_Static_assert(NET_MAX_BUFFERS >= MAX(RX_QUEUE_SIZE_DRIV, MAX(RX_QUEUE_SIZE_CLI0, RX_QUEUE_SIZE_CLI1)), "Shared queue capacity must be >=  largest RX queue.");
_Static_assert(TX_QUEUE_SIZE_DRIV >= TX_QUEUE_SIZE_ARP + TX_QUEUE_SIZE_CLI0 + TX_QUEUE_SIZE_CLI1, "Driver TX queue must have capacity to fit all of client's TX buffers.");
_Static_assert(RX_QUEUE_SIZE_ARP >= RX_QUEUE_SIZE_DRIV, "Arp queue must have capacity to fit all rx buffers.");
_Static_assert(RX_QUEUE_SIZE_COPY0 >= RX_QUEUE_SIZE_DRIV, "Copy0 queue must have capacity to fit all RX buffers.");
_Static_assert(RX_QUEUE_SIZE_COPY1 >= RX_QUEUE_SIZE_DRIV, "Copy1 queue must have capacity to fit all RX buffers.");
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

static void cli_mac_addr_init_sys(char *pd_name, uint8_t *macs)
{
    if (__str_match(pd_name, CLI0_NAME)) {
        __set_mac_addr(macs, MAC_ADDR_CLI0);
    } else if (__str_match(pd_name, CLI1_NAME)) {
        __set_mac_addr(macs, MAC_ADDR_CLI1);
    } 
}

static void arp_mac_addr_init_sys(char *pd_name, uint8_t *macs)
{
    if (__str_match(pd_name, ARP_NAME)) {
        __set_mac_addr(macs, MAC_ADDR_CLI0);
        __set_mac_addr(&macs[ETH_HWADDR_LEN], MAC_ADDR_CLI1);
    } 
}

static void virt_mac_addr_init_sys(char *pd_name, uint8_t *macs)
{
    if (__str_match(pd_name, VIRT_RX_NAME)) {
        __set_mac_addr(macs, MAC_ADDR_ARP);
        __set_mac_addr(&macs[ETH_HWADDR_LEN], MAC_ADDR_CLI0);
        __set_mac_addr(&macs[2*ETH_HWADDR_LEN], MAC_ADDR_CLI1);
    }
}

static void cli_queue_init_sys(char *pd_name, net_queue_handle_t *rx_queue, uintptr_t rx_free, uintptr_t rx_active,
                                net_queue_handle_t *tx_queue, uintptr_t tx_free, uintptr_t tx_active)
{
    if (__str_match(pd_name, CLI0_NAME)) {
        net_queue_init(rx_queue, (net_queue_t *) rx_free, (net_queue_t *) rx_active, RX_QUEUE_SIZE_CLI0);
        net_queue_init(tx_queue, (net_queue_t *) tx_free, (net_queue_t *) tx_active, TX_QUEUE_SIZE_CLI0);
    } else if (__str_match(pd_name, CLI1_NAME)) {
        net_queue_init(rx_queue, (net_queue_t *) rx_free, (net_queue_t *) rx_active, RX_QUEUE_SIZE_CLI1);
        net_queue_init(tx_queue, (net_queue_t *) tx_free, (net_queue_t *) tx_active, TX_QUEUE_SIZE_CLI1);
    }
}

static void copy_queue_init_sys(char *pd_name, net_queue_handle_t *cli_queue, uintptr_t cli_free, uintptr_t cli_active,
                                net_queue_handle_t *virt_queue, uintptr_t virt_free, uintptr_t virt_active)
{
    if (__str_match(pd_name, COPY0_NAME)) {
        net_queue_init(cli_queue, (net_queue_t *) cli_free, (net_queue_t *) cli_active, RX_QUEUE_SIZE_CLI0);
        net_queue_init(virt_queue, (net_queue_t *) virt_free, (net_queue_t *) virt_active, RX_QUEUE_SIZE_COPY0);
    } else if (__str_match(pd_name, COPY1_NAME)) {
        net_queue_init(cli_queue, (net_queue_t *) cli_free, (net_queue_t *) cli_active, RX_QUEUE_SIZE_CLI1);
        net_queue_init(virt_queue, (net_queue_t *) virt_free, (net_queue_t *) virt_active, RX_QUEUE_SIZE_COPY1);
    }
}

static void virt_queue_init_sys(char *pd_name, net_queue_handle_t *cli_queue, uintptr_t cli_free, uintptr_t cli_active)
{
    if (__str_match(pd_name, VIRT_RX_NAME)) {
        net_queue_init(cli_queue, (net_queue_t *) cli_free, (net_queue_t *) cli_active, RX_QUEUE_SIZE_ARP);
        net_queue_init(&cli_queue[1], (net_queue_t *) (cli_free + 2 * DATA_REGION_SIZE), (net_queue_t *) (cli_active + 2 * DATA_REGION_SIZE), RX_QUEUE_SIZE_CLI0);
        net_queue_init(&cli_queue[2], (net_queue_t *) (cli_free + 4 * DATA_REGION_SIZE), (net_queue_t *) (cli_active + 4 * DATA_REGION_SIZE), RX_QUEUE_SIZE_CLI1);
    } else if (__str_match(pd_name, VIRT_TX_NAME)) {
        net_queue_init(cli_queue, (net_queue_t *) cli_free, (net_queue_t *) cli_active, TX_QUEUE_SIZE_ARP);
        net_queue_init(&cli_queue[1], (net_queue_t *) (cli_free + 2 * DATA_REGION_SIZE), (net_queue_t *) (cli_active + 2 * DATA_REGION_SIZE), TX_QUEUE_SIZE_CLI0);
        net_queue_init(&cli_queue[2], (net_queue_t *) (cli_free + 4 * DATA_REGION_SIZE), (net_queue_t *) (cli_active + 4 * DATA_REGION_SIZE), TX_QUEUE_SIZE_CLI1);
    }
}

static void mem_region_init_sys(char *pd_name, uintptr_t *mem_regions, uintptr_t start_region) {
    if (__str_match(pd_name, VIRT_TX_NAME)) {
        mem_regions[0] = start_region;
        mem_regions[1] = start_region + DATA_REGION_SIZE;
        mem_regions[2] = start_region + DATA_REGION_SIZE * 2;
    }
}
