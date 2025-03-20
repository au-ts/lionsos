#pragma once

#include <stdint.h>
#include <sddf/network/constants.h>

#define ETH_HWTYPE 1
#define ETH_TYPE_IP 0x0800U
#define ETH_TYPE_ARP 0x0806U

#define IPV4_PROTO_LEN 4
#define IPV4_PROTO_ICMP 0x01
#define IPV4_PROTO_TCP 0x06
#define IPV4_PROTO_UDP 0x11

#define ETHARP_OPCODE_REQUEST 1
#define ETHARP_OPCODE_REPLY 2

typedef struct __attribute__((__packed__)) ipv4_packet {
    uint8_t ethdst_addr[ETH_HWADDR_LEN];
    uint8_t ethsrc_addr[ETH_HWADDR_LEN];
    uint16_t type;
    unsigned int ihl:4;
    unsigned int version:4;
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

static uint8_t transport_layer_offset(ipv4_packet_t *ip_pkt) {
    return sizeof(struct ethernet_header) + 4 * ip_pkt->ihl;
}

typedef struct __attribute__((__packed__)) udphdr
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t check;
} udphdr_t;

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

typedef struct __attribute__((__packed__)) icmphdr
{
    uint8_t type;		    /* message type */
    uint8_t code;		    /* type sub-code */
    uint16_t checksum;
    union
    {
        struct
        {
            uint16_t id;
            uint16_t sequence;
        } echo;			    /* echo datagram */
        uint32_t gateway;	/* gateway address */
        struct
        {
            uint16_t unused;
            uint16_t mtu;
        } frag;			    /* path mtu discovery */
    } un;
} icmphdr_t;

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
