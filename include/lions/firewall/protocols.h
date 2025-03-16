#pragma once

#include <stdint.h>
#include <sddf/network/constants.h>

#define ETH_HWTYPE 1
#define ETH_TYPE_IP 0x0800U
#define ETH_TYPE_ARP 0x0806U

#define IPV4_PROTO_LEN 4

#define ETHARP_OPCODE_REQUEST 1
#define ETHARP_OPCODE_REPLY 2

#define IPV4_ADDR(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((uint32_t) (d) << 24))

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
    return ip_pkt->ihl;
}

typedef struct __attribute__((__packed__)) udp_hdr
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t check;
} udp_hdr_t;

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

typedef __attribute__((__packed__)) struct icmphdr
{
    uint8_t type;		    /* message type */
    uint8_t code;		    /* type sub-code */
    uint16_t checksum;
    union
    {
        struct
        {
            u_int16_t id;
            u_int16_t sequence;
        } echo;			    /* echo datagram */
        u_int32_t gateway;	/* gateway address */
        struct
        {
            u_int16_t __unused;
            u_int16_t mtu;
        } frag;			    /* path mtu discovery */
    } un;
} icmphdr_t;
