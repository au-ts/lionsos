#pragma once

#include <sddf/network/shared_ringbuffer.h>

#define NUM_CLIENTS 3

#define ARP_NAME "arp"
#define CLI0_NAME "nfs"
#define CLI1_NAME "micropython"
#define COPY0_NAME "eth_copy_nfs"
#define COPY1_NAME "eth_copy_mp"
#define MUX_RX_NAME "eth_mux_rx"
#define MUX_TX_NAME "eth_mux_tx"
#define DRIVER_NAME "eth"

#define DATA_REGION_SIZE                    0x200000
#define HW_REGION_SIZE                      0x10000

#define MAC_ADDR_ARP                        0xFFFFFFFFFFFF
#define MAC_ADDR_CLI0                       0x525401000010
#define MAC_ADDR_CLI1                       0x525401000011

#define TX_RING_SIZE_ARP                    512
#define TX_RING_SIZE_CLI0                   512
#define TX_RING_SIZE_CLI1                   512
#define TX_RING_SIZE_DRIV                   (TX_RING_SIZE_ARP + TX_RING_SIZE_CLI0 + TX_RING_SIZE_CLI1)

#define TX_DATA_REGION_SIZE_ARP             DATA_REGION_SIZE
#define TX_DATA_REGION_SIZE_CLI0            DATA_REGION_SIZE
#define TX_DATA_REGION_SIZE_CLI1            DATA_REGION_SIZE

_Static_assert(TX_DATA_REGION_SIZE_ARP >= TX_RING_SIZE_ARP * BUFF_SIZE, "Arp TX data region size must fit Arp TX buffers");
_Static_assert(TX_DATA_REGION_SIZE_CLI0 >= TX_RING_SIZE_CLI0 * BUFF_SIZE, "Client0 TX data region size must fit Client0 TX buffers");
_Static_assert(TX_DATA_REGION_SIZE_CLI1 >= TX_RING_SIZE_CLI1 * BUFF_SIZE, "Client1 TX data region size must fit Client1 TX buffers");

#define RX_RING_SIZE_DRIV                   512
#define RX_RING_SIZE_ARP                    RX_RING_SIZE_DRIV
#define RX_RING_SIZE_CLI0                   512
#define RX_RING_SIZE_CLI1                   512
#define RX_RING_SIZE_COPY0                  RX_RING_SIZE_DRIV
#define RX_RING_SIZE_COPY1                  RX_RING_SIZE_DRIV

#define RX_DATA_REGION_SIZE_DRIV            DATA_REGION_SIZE
#define RX_DATA_REGION_SIZE_CLI0            DATA_REGION_SIZE
#define RX_DATA_REGION_SIZE_CLI1            DATA_REGION_SIZE

_Static_assert(RX_DATA_REGION_SIZE_DRIV >= RX_RING_SIZE_DRIV * BUFF_SIZE, "Driver RX data region size must fit Driver RX buffers");
_Static_assert(RX_DATA_REGION_SIZE_CLI0 >= RX_RING_SIZE_CLI0 * BUFF_SIZE, "Client0 RX data region size must fit Client0 RX buffers");
_Static_assert(RX_DATA_REGION_SIZE_CLI1 >= RX_RING_SIZE_CLI1 * BUFF_SIZE, "Client1 RX data region size must fit Client1 RX buffers");

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

_Static_assert(MAX_BUFFS >= TX_RING_SIZE_DRIV, "Shared ring buffer capacity must be >= largest TX ring.");
_Static_assert(MAX_BUFFS >= MAX(RX_RING_SIZE_DRIV, MAX(RX_RING_SIZE_CLI0, RX_RING_SIZE_CLI1)), "Shared ring buffer capacity must be >=  largest RX ring.");
_Static_assert(TX_RING_SIZE_DRIV >= TX_RING_SIZE_ARP + TX_RING_SIZE_CLI0 + TX_RING_SIZE_CLI1, "Driver TX ring buffer must have capacity to fit all of client's TX buffers.");
_Static_assert(RX_RING_SIZE_ARP >= RX_RING_SIZE_DRIV, "Arp ring buffers must have capacity to fit all rx buffers.");
_Static_assert(RX_RING_SIZE_COPY0 >= RX_RING_SIZE_DRIV, "Copy0 ring buffers must have capacity to fit all RX buffers.");
_Static_assert(RX_RING_SIZE_COPY1 >= RX_RING_SIZE_DRIV, "Copy1 ring buffers must have capacity to fit all RX buffers.");
_Static_assert(sizeof(ring_buffer_t) <= DATA_REGION_SIZE, "Ring buffer must fit into a single data region.");

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

static void mux_mac_addr_init_sys(char *pd_name, uint8_t *macs)
{
    if (__str_match(pd_name, MUX_RX_NAME)) {
        __set_mac_addr(macs, MAC_ADDR_ARP);
        __set_mac_addr(&macs[ETH_HWADDR_LEN], MAC_ADDR_CLI0);
        __set_mac_addr(&macs[2*ETH_HWADDR_LEN], MAC_ADDR_CLI1);
    }
}

static void cli_ring_init_sys(char *pd_name, ring_handle_t *rx_ring, uintptr_t rx_free, uintptr_t rx_used,
                                ring_handle_t *tx_ring, uintptr_t tx_free, uintptr_t tx_used)
{
    if (__str_match(pd_name, CLI0_NAME)) {
        ring_init(rx_ring, (ring_buffer_t *) rx_free, (ring_buffer_t *) rx_used, RX_RING_SIZE_CLI0);
        ring_init(tx_ring, (ring_buffer_t *) tx_free, (ring_buffer_t *) tx_used, TX_RING_SIZE_CLI0);
    } else if (__str_match(pd_name, CLI1_NAME)) {
        ring_init(rx_ring, (ring_buffer_t *) rx_free, (ring_buffer_t *) rx_used, RX_RING_SIZE_CLI1);
        ring_init(tx_ring, (ring_buffer_t *) tx_free, (ring_buffer_t *) tx_used, TX_RING_SIZE_CLI1);
    }
}

static void copy_ring_init_sys(char *pd_name, ring_handle_t *cli_ring, uintptr_t cli_free, uintptr_t cli_used,
                                ring_handle_t *mux_ring, uintptr_t mux_free, uintptr_t mux_used)
{
    if (__str_match(pd_name, COPY0_NAME)) {
        ring_init(cli_ring, (ring_buffer_t *) cli_free, (ring_buffer_t *) cli_used, RX_RING_SIZE_CLI0);
        ring_init(mux_ring, (ring_buffer_t *) mux_free, (ring_buffer_t *) mux_used, RX_RING_SIZE_COPY0);
    } else if (__str_match(pd_name, COPY1_NAME)) {
        ring_init(cli_ring, (ring_buffer_t *) cli_free, (ring_buffer_t *) cli_used, RX_RING_SIZE_CLI1);
        ring_init(mux_ring, (ring_buffer_t *) mux_free, (ring_buffer_t *) mux_used, RX_RING_SIZE_COPY1);
    }
}

static void mux_ring_init_sys(char *pd_name, ring_handle_t *cli_ring, uintptr_t cli_free, uintptr_t cli_used)
{
    if (__str_match(pd_name, MUX_RX_NAME)) {
        ring_init(cli_ring, (ring_buffer_t *) cli_free, (ring_buffer_t *) cli_used, RX_RING_SIZE_ARP);
        ring_init(&cli_ring[1], (ring_buffer_t *) (cli_free + 2 * DATA_REGION_SIZE), (ring_buffer_t *) (cli_used + 2 * DATA_REGION_SIZE), RX_RING_SIZE_CLI0);
        ring_init(&cli_ring[2], (ring_buffer_t *) (cli_free + 4 * DATA_REGION_SIZE), (ring_buffer_t *) (cli_used + 4 * DATA_REGION_SIZE), RX_RING_SIZE_CLI1);
    } else if (__str_match(pd_name, MUX_TX_NAME)) {
        ring_init(cli_ring, (ring_buffer_t *) cli_free, (ring_buffer_t *) cli_used, TX_RING_SIZE_ARP);
        ring_init(&cli_ring[1], (ring_buffer_t *) (cli_free + 2 * DATA_REGION_SIZE), (ring_buffer_t *) (cli_used + 2 * DATA_REGION_SIZE), TX_RING_SIZE_CLI0);
        ring_init(&cli_ring[2], (ring_buffer_t *) (cli_free + 4 * DATA_REGION_SIZE), (ring_buffer_t *) (cli_used + 4 * DATA_REGION_SIZE), TX_RING_SIZE_CLI1);
    }
}

static void mem_region_init_sys(char *pd_name, uintptr_t *mem_regions, uintptr_t start_region) {
    if (__str_match(pd_name, MUX_TX_NAME)) {
        mem_regions[0] = start_region;
        mem_regions[1] = start_region + DATA_REGION_SIZE;
        mem_regions[2] = start_region + DATA_REGION_SIZE * 2;
    }
}