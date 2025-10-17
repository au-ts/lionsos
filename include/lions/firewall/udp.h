/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/firewall/ip.h>

/* ----------------- UDP Protocol Definitions ---------------------------*/

typedef struct __attribute__((__packed__)) udp_hdr
{
    /* source port */
    uint16_t src_port;
    /* destination port */
    uint16_t dst_port;
    /* length in bytes of the UDP datagram including header */
    uint16_t len;
    /* checksum over the UDP datagram and psuedo-header, optional for IPv4 */
    uint16_t check;
} udp_hdr_t;
