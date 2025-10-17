/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/firewall/ip.h>

/* ----------------- TCP Protocol Definitions ---------------------------*/

typedef struct __attribute__((__packed__)) tcp_hdr
  {
    /* source port */
    uint16_t src_port;
    /* destination port */
    uint16_t dst_port;
    /* sequence number */
    uint32_t seq;
    /* acknowledgement number */
    uint32_t ack_seq;
    /* reserved, set to 0 */
    uint16_t reserved:4;
    /* size of the TCP header in 32 bit words */
    uint16_t doff:4;
    /* fin */
    uint16_t fin:1;
    /* syn */
    uint16_t syn:1;
    /* reset */
    uint16_t rst:1;
    /* push */
    uint16_t psh:1;
    /* ack */
    uint16_t ack:1;
    /* urgent pointer is valid */
    uint16_t urg:1;
    /* ECN-Echo*/
    uint16_t ece:1;
    /* congestion window reduced */
    uint16_t cwr:1;
    /* size of the receive window*/
    uint16_t window;
    /* checksum over the TCP packet and psuedo-header */
    uint16_t check;
    /* urgent pointer */
    uint16_t urg_ptr;
    /* optional fields excluded */
} tcp_hdr_t;
