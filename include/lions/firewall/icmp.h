/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/firewall/protocols.h>

#define FW_ICMP_OLD_DATA_LEN 8
typedef struct icmp_req {
    /* Type of ICMP packet to send */
    uint8_t type;
    /* Code of ICMP packet to sent */
    uint8_t code;
    /* Original header associated with ICMP packet */
    ipv4_packet_t hdr;
    /* First 8 bytes of data from original packet */
    uint8_t data[FW_ICMP_OLD_DATA_LEN];
} icmp_req_t;
