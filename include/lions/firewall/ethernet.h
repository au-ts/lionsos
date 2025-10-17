/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/network/constants.h>

/* ----------------- Ethernet Protocol Definitions ---------------------------*/

typedef struct __attribute__((__packed__)) eth_hdr {
    /* destination MAC address */
    uint8_t ethdst_addr[ETH_HWADDR_LEN];
    /* source MAC address */
    uint8_t ethsrc_addr[ETH_HWADDR_LEN];
    /* if ethtype <= 1500 it holds the length of the frame. Otherwise, it holds
    the protocol of payload encapsulated in the frame */
    uint16_t ethtype;
} eth_hdr_t;

/* Length of ethernet header */
#define ETH_HDR_LEN sizeof(eth_hdr_t)

/* Ethernet type values */
#define ETH_TYPE_IP 0x0800U /* IPV4 packet */
#define ETH_TYPE_ARP 0x0806U /* ARP packet */
