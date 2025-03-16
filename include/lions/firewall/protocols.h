#pragma once

#include <stdint.h>


#define ETH_HWTYPE 1
#define ETH_TYPE_ARP 0x0806U
#define ETH_TYPE_IP 0x0800U
#define IPV4_PROTO_LEN 4
#define ETH_HWADDR_LEN 6
#define ETHARP_OPCODE_REQUEST 1
#define ETHARP_OPCODE_REPLY 2
#define IPV4_ADDR(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((uint32_t) (d) << 24))
#define NET_BUFFER_SIZE 2048

struct __attribute__((__packed__)) ipv4_packet {
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
};

struct __attribute__((__packed__)) arp_packet {
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
};

struct __attribute__((__packed__)) tcp_packet {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    unsigned int data_off:4;
    unsigned int ctrl:9;
    uint16_t wndw_sz;
    uint16_t check;
    uint16_t urgent_ptr;
};

struct __attribute__((__packed__)) ppp_header {
    uint8_t flag;
    uint8_t address;
    uint8_t control;
    uint16_t protocol;
};
