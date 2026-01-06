/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/firewall/ip.h>

/* ----------------- ICMP Protocol Definitions ---------------------------*/

/* Shared ICMP header prefix accross all control types */
typedef struct __attribute__((__packed__)) icmp_hdr {
    /* ICMP type */
    uint8_t type;
    /* ICMP sub-type */
    uint8_t code;
    /* internet checksum calculated over ICMP header and data */
    uint16_t check;
    /* The following 4 bytes of the header are ICMP type dependent */
} icmp_hdr_t;

/* Offset of the start of the ICMP header, when IPv4 header is minimum size */
#define ICMP_HDR_OFFSET (IPV4_HDR_OFFSET + IPV4_HDR_LEN_MIN)

/* Length of ICMP common header */
#define ICMP_COMMON_HDR_LEN sizeof(icmp_hdr_t)

/* ICMP control types */
#define ICMP_ECHO_REPLY 0
#define ICMP_DEST_UNREACHABLE 3
#define ICMP_SRC_QUENCH 4
#define ICMP_REDIRECT_MSG 5
#define ICMP_ECHO_REQ 8
#define ICMP_ROUTER_AD 9
#define ICMP_ROUTER_SOLIT 10
#define ICMP_ROUTER_SOLIT 10
#define ICMP_TTL_EXCEED 11

/* ICMP destination unreachable sub-type codes */
#define ICMP_DEST_NET_UNREACHABLE 0
#define ICMP_DEST_HOST_UNREACHABLE 1
#define ICMP_DEST_PROTO_UNREACHABLE 2
#define ICMP_DEST_PORT_UNREACHABLE 3
#define ICMP_DEST_FRAG_REQ 4
#define ICMP_SRC_ROUTE_FAIL 5
#define ICMP_DEST_NET_UNKNOWN 6
#define ICMP_DEST_HOST_UNKNOWN 7
#define ICMP_SRC_HOST_ISOLATED 8
#define ICMP_NET_ADMIN_PROHIBITED 9
#define ICMP_HOST_ADMIN_PROHIBITED 10

/* ICMP Time Exceeded sub-type codes */
#define ICMP_TIME_EXCEEDED_TTL 0
#define ICMP_TIME_EXCEEDED_FRAG 1
/* ----------------- 3 - Destination Unreachable ---------------------------*/

/* Default number of bytes included from source packet in destination
unreachable replies */
#define FW_ICMP_SRC_DATA_LEN 8

typedef struct __attribute__((__packed__)) icmp_dest {
    /* unused, must be set to 0 */
    uint8_t unused;
    /* optional length of source packet in 32-bit words, or 0 */
    uint8_t len;
    /* optional MTU of the next-hop network if source packet was too large, or 0
    */
    uint16_t nexthop_mtu;
    /* IP header of source packet */
    ipv4_hdr_t ip_hdr;
    /* first 8 bytes of data from source packet */
    uint8_t data[FW_ICMP_SRC_DATA_LEN];
} icmp_dest_t;

/* Offset of the start of the ICMP destination unreachable sub-header */
#define ICMP_DEST_OFFSET (ICMP_HDR_OFFSET + ICMP_COMMON_HDR_LEN)

/* Total length of ICMP destination unreachable header */
#define ICMP_DEST_LEN (ICMP_COMMON_HDR_LEN + sizeof(icmp_dest_t))

/* ----------------- 8 - Echo Request / 0 - Echo Reply ---------------------------*/

/* ICMP echo request/reply header fields (following common header) */
typedef struct __attribute__((__packed__)) icmp_echo {
    /* Identifier to match requests with replies */
    uint16_t id;
    /* Sequence number */
    uint16_t seq;
    /* Payload data follows */
} icmp_echo_t;

/* Offset of the start of the ICMP echo sub-header */
#define ICMP_ECHO_OFFSET (ICMP_HDR_OFFSET + ICMP_COMMON_HDR_LEN)

/* Maximum payload length for ICMP echo messages */
#define FW_ICMP_ECHO_PAYLOAD_LEN 56

/* ----------------- 11 - Time Exceeded ---------------------------*/
/* ICMP Time Exceeded header fields*/
typedef struct __attribute__((__packed__)) icmp_time_exceeded {
    /* unused, must be set to 0 */
    uint32_t unused;
    /* IP header of source packet */
    ipv4_hdr_t ip_hdr;
    /* First 8 bytes of data from source packet */
    uint8_t data[FW_ICMP_SRC_DATA_LEN];
} icmp_time_exceeded_t;

#define ICMP_TIME_EXCEEDED_LEN (ICMP_COMMON_HDR_LEN + sizeof(icmp_time_exceeded_t))

#define ICMP_TIME_EXCEEDED_OFFSET (ICMP_HDR_OFFSET + ICMP_COMMON_HDR_LEN)

/* ----------------- Firewall Data Types ---------------------------*/

/* ICMP destination unreachable request data */
typedef struct {
    /* first 8 bytes of data from source packet */
    uint8_t data[FW_ICMP_SRC_DATA_LEN];
} icmp_req_dest_t;

/* ICMP echo request/reply data */
typedef struct {
    /* Identifier to match requests with replies */
    uint16_t echo_id;
    /* Sequence number */
    uint16_t echo_seq;
    /* Payload length */
    uint16_t payload_len;
    /* Echo payload data */
    uint8_t data[FW_ICMP_ECHO_PAYLOAD_LEN];
} icmp_req_echo_t;

/* ICMP time exceeded data */
typedef struct {
    /* first 8 bytes of data from source packet */
    uint8_t data[FW_ICMP_SRC_DATA_LEN];
} icmp_req_time_exceeded_t;

/* Data type of ICMP queues used to request transmission of ICMP packets */
typedef struct icmp_req {
    /* type of ICMP packet to send */
    uint8_t type;
    /* code of ICMP packet to send */
    uint8_t code;
    /* ethernet header of request source packet */
    eth_hdr_t eth_hdr;
    /* header of source IP packet */
    ipv4_hdr_t ip_hdr;
    /* Type-specific data */
    union {
        icmp_req_dest_t dest;
        icmp_req_echo_t echo;
        icmp_req_time_exceeded_t time_exceeded;
    };
} icmp_req_t;
