/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/network/constants.h>

/* ethernet types */
#define ETH_HWTYPE 1
#define ETH_TYPE_IP 0x0800U
#define ETH_TYPE_ARP 0x0806U

/* IP protocols */
#define IPV4_PROTO_LEN 4
#define IPV4_PROTO_ICMP 0x01
#define IPV4_PROTO_TCP 0x06
#define IPV4_PROTO_UDP 0x11

/* arp types */
#define ETHARP_OPCODE_REQUEST 1
#define ETHARP_OPCODE_REPLY 2

/* ICMP Control Types. */
#define ICMP_ECHO_REPLY 0
#define ICMP_DEST_UNREACHABLE 3
#define ICMP_SRC_QUENCH 4
#define ICMP_REDIRECT_MSG 5
#define ICMP_ECHO_REQ 8
#define ICMP_ROUTER_AD 9
#define ICMP_ROUTER_SOLIT 10
/* @kwinter: Fill out the rest of these ICMP definitions. */

/* ICMP Destination Unreachable Subtypes. */
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

/* IP packet including ethernet header */
typedef struct __attribute__((__packed__)) ipv4_packet {
    uint8_t ethdst_addr[ETH_HWADDR_LEN];
    uint8_t ethsrc_addr[ETH_HWADDR_LEN];
    uint16_t type;
    uint8_t ihl_version;
    uint8_t tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t check;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_packet_t;

/* IP packet header */
typedef struct __attribute__((__packed__)) ipv4hdr {
    uint8_t ihl_version;
    uint8_t tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t check;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4hdr_t;

/* arp packet including ethernet header */
typedef struct __attribute__((__packed__)) arp_packet {
      uint8_t ethdst_addr[ETH_HWADDR_LEN];
      uint8_t ethsrc_addr[ETH_HWADDR_LEN];
      uint16_t type;
      uint16_t hwtype;
      uint16_t proto;
      uint8_t hwlen;
      uint8_t protolen;
      uint16_t opcode;
      uint8_t hwsrc_addr[ETH_HWADDR_LEN];
      uint32_t ipsrc_addr;
      uint8_t hwdst_addr[ETH_HWADDR_LEN];
      uint32_t ipdst_addr;
      uint8_t padding[10];
      uint32_t crc;
} arp_packet_t;

/* arp header */
typedef struct __attribute__((__packed__)) arphdr {
    uint16_t hwtype;
    uint16_t proto;
    uint8_t hwlen;
    uint8_t protolen;
    uint16_t opcode;
    uint8_t hwsrc_addr[ETH_HWADDR_LEN];
    uint32_t ipsrc_addr;
    uint8_t hwdst_addr[ETH_HWADDR_LEN];
    uint32_t ipdst_addr;
} arphdr_t;

/* udp header */
typedef struct __attribute__((__packed__)) udphdr
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t check;
} udphdr_t;

/* tcp header */
typedef struct __attribute__((__packed__)) tcphdr
  {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack_seq;
    uint16_t res1:4;
    uint16_t doff:4;
    uint16_t fin:1;
    uint16_t syn:1;
    uint16_t rst:1;
    uint16_t psh:1;
    uint16_t ack:1;
    uint16_t urg:1;
    uint16_t res2:2;
    uint16_t window;
    uint16_t check;
    uint16_t urg_ptr;
} tcphdr_t;

/* icmp packet including ethernet header */
typedef struct __attribute__((__packed__)) icmp_packet
{
    uint8_t ethdst_addr[ETH_HWADDR_LEN];
    uint8_t ethsrc_addr[ETH_HWADDR_LEN];
    uint16_t eth_type;
    uint8_t ihl_version;
    uint8_t tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t check;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t type;		    /* message type */
    uint8_t code;		    /* type sub-code */
    uint16_t checksum;
    // 4-byte padding boundary
    uint32_t _unused;
    ipv4hdr_t old_ip_hdr;
    uint64_t old_data;
} icmp_packet_t;

/**
 * Extract offset of IP protocol header from IP packet.
 *
 * @param ip_pkt address of IP packet.
 *
 * @return offset of IP protocol header.
 */
static uint8_t transport_layer_offset(ipv4_packet_t *ip_pkt)
{
    return sizeof(struct ethernet_header) + 4 * (ip_pkt->ihl_version & 0xF);
}
