/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/firewall/ethernet.h>

/* ----------------- IP Protocol Definitions ---------------------------*/

typedef struct __attribute__((__packed__)) ipv4_hdr {
    /* internet header length in 32-bit words, variable due to optional fields */
    uint8_t ihl:4;
    /* IP version, always 4 for IPv4 */
    uint8_t version:4;
    /* explicit congestion notification, optional */
    uint8_t ecn:2;
    /* differentiated services code point */
    uint8_t dscp:6;
    /* total packet length in bytes, including header and data */
    uint16_t tot_len;
    /* identifier of packet, used in packet fragmentation */
    uint16_t id;
    /* offset in 8 bytes of fragment relative to the beginning of the original
    unfragmented IP datagram. Fragment offset is a 13 byte value split accross
    frag_offset1 and frag_offset2 */
    uint8_t frag_offset1:5;
    /* if packet belongs to fragmented group, 1 indicates this is not the last
    fragment */
    uint8_t more_frag:1;
    /* specifies whether datagram can be fragmented or not */
    uint8_t no_frag:1;
    /* reserved, set to 0 */
    uint8_t reserved:1;
    /* offset in 8 bytes of fragment relative to the beginning of the original
    unfragmented IP datagram. Fragment offset is a 13 byte value split accross
    frag_offset1 and frag_offset2 */
    uint8_t frag_offset2;
    /* time to live, in seconds but in practice router hops */
    uint8_t ttl;
    /* transport layer protocol of encapsulated packet */
    uint8_t protocol;
    /* internet checksum of IPv4 header */
    uint16_t check;
    /* source IP address */
    uint32_t src_ip;
    /* destination IP address */
    uint32_t dst_ip;
    /* optional fields excluded */
} ipv4_hdr_t;

/* Offset of the start of the IPV4 header */
#define IPV4_HDR_OFFSET ETH_HDR_LEN

/* Length of IPv4 header with no optional fields */
#define IPV4_HDR_LEN_MIN sizeof(ipv4_hdr_t)

/* IPv4 differentiated services code point values */
#define IPV4_DSCP_NET_CTRL 48 /* Network control */

/* IPv4 transport layer protocols */
#define IPV4_PROTO_ICMP 0x01
#define IPV4_PROTO_TCP 0x06
#define IPV4_PROTO_UDP 0x11

/**
 * IPv4 header length in bytes.
 *
 * @param ip_hdr address of IP packet.
 *
 * @return IPv4 header length in bytes.
 */
static inline uint8_t ipv4_header_length(ipv4_hdr_t *ip_hdr)
{
    return 4 * ip_hdr->ihl;
}

/**
 * Extract offset of transport layer header from IP packet.
 *
 * @param ip_hdr address of IP packet.
 *
 * @return offset in bytes transport layer header.
 */
static inline uint8_t transport_layer_offset(ipv4_hdr_t *ip_hdr)
{
    return IPV4_HDR_OFFSET + ipv4_header_length(ip_hdr);
}
