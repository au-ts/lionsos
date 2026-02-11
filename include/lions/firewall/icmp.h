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
#define ICMP_PARAM_PROBLEM 12

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

/* ICMP Redirect sub-type codes */
#define ICMP_REDIRECT_FOR_NET 0
#define ICMP_REDIRECT_FOR_HOST 1
#define ICMP_REDIRECT_FOR_TOS_NET 2
#define ICMP_REDIRECT_FOR_TOS_HOST 3

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

/* ----------------- 5 - ICMP REDIRECT MSG ---------------------------*/
/* ICMP Time Exceeded header fields*/
typedef struct __attribute__((__packed__)) icmp_redirect {
    /* IP Address of the new path */
    uint32_t gateway_ip;
    /* IP header of source packet */
    ipv4_hdr_t ip_hdr;
    /* First 8 bytes of data from source packet */
    uint8_t data[FW_ICMP_SRC_DATA_LEN];
} icmp_redirect_t;

#define ICMP_REDIRECT_LEN (ICMP_COMMON_HDR_LEN + sizeof(icmp_redirect_t))

#define ICMP_REDIRECT_OFFSET (ICMP_HDR_OFFSET + ICMP_COMMON_HDR_LEN)


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

/* Total length of ICMP echo request/reply packet with maximum payload */
#define ICMP_ECHO_LEN (ICMP_COMMON_HDR_LEN + sizeof(icmp_echo_t) + FW_ICMP_ECHO_PAYLOAD_LEN)

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

/* ICMP redirect data */
typedef struct {
    /* new gateway IP address */
    uint32_t gateway_ip;
    /* first 8 bytes of data from source packet */
    uint8_t data[FW_ICMP_SRC_DATA_LEN];
} icmp_req_redirect_t;

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
        icmp_req_redirect_t redirect;
    };
} icmp_req_t;


/**
 * Check if ICMP type is an error message that should not trigger redirects.
 * Per RFC 1812, redirects should not be sent for ICMP error messages.
 *
 * @param type ICMP type value
 *
 * @return true if type is an ICMP error message, false otherwise
 */
static inline bool icmp_is_error_message(uint8_t type)
{
    return (type == ICMP_DEST_UNREACHABLE ||
            type == ICMP_REDIRECT_MSG ||
            type == ICMP_SRC_QUENCH ||
            type == ICMP_TTL_EXCEED ||
            type == ICMP_PARAM_PROBLEM);
}



/**
 * Enqueue an ICMP request to send back to the source.
 * This is a generic helper for destination unreachable, time exceeded, etc.
 *
 * @param icmp_queue Pointer to the ICMP queue to enqueue the request.
 * @param type ICMP type (e.g., ICMP_DEST_UNREACHABLE, ICMP_TTL_EXCEED).
 * @param code ICMP code (e.g., ICMP_DEST_PORT_UNREACHABLE).
 * @param pkt_vaddr Virtual address of the packet data.
 *
 * @return 0 on success, non-zero on failure.
 */
static inline int icmp_enqueue_error(fw_queue_t *icmp_queue, uint8_t type, uint8_t code, uintptr_t pkt_vaddr)
{
    icmp_req_t req = {0};
    req.type = type;
    req.code = code;

    /* Copy ethernet header into ICMP request */
    memcpy(&req.eth_hdr, (void *)pkt_vaddr, ETH_HDR_LEN);

    /* Copy IP header into ICMP request */
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
    memcpy(&req.ip_hdr, (void *)ip_hdr, IPV4_HDR_LEN_MIN);

    /* Copy first bytes of data if applicable */
    uint16_t to_copy = MIN(FW_ICMP_SRC_DATA_LEN, htons(ip_hdr->tot_len) - IPV4_HDR_LEN_MIN);
    memcpy(req.dest.data, (void *)(pkt_vaddr + IPV4_HDR_OFFSET + IPV4_HDR_LEN_MIN), to_copy);

    return fw_enqueue(icmp_queue, &req);
}

/**
 * Enqueue an ICMP echo reply request.
 *
 * @param icmp_queue Pointer to the ICMP queue to enqueue the request.
 * @param pkt_vaddr Virtual address of the packet data.
 *
 * @return 0 on success, non-zero on failure.
 */
static inline int icmp_enqueue_echo_reply(fw_queue_t *icmp_queue, uintptr_t pkt_vaddr)
{
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);

    /* Extract echo id and sequence from the ICMP echo header */
    icmp_echo_t *echo_hdr = (icmp_echo_t *)(pkt_vaddr + ICMP_ECHO_OFFSET);
    uint16_t echo_id = ntohs(echo_hdr->id);
    uint16_t echo_seq = ntohs(echo_hdr->seq);

    /* Calculate payload length */
    uint16_t icmp_total_len = htons(ip_hdr->tot_len) - ipv4_header_length(ip_hdr);
    uint16_t payload_len = icmp_total_len - ICMP_COMMON_HDR_LEN - sizeof(icmp_echo_t);
    if (payload_len > FW_ICMP_ECHO_PAYLOAD_LEN) {
        payload_len = FW_ICMP_ECHO_PAYLOAD_LEN;
    }

    icmp_req_t req = {0};
    req.type = ICMP_ECHO_REPLY;
    req.code = 0;
    req.echo.echo_id = echo_id;
    req.echo.echo_seq = echo_seq;
    req.echo.payload_len = payload_len;

    /* Copy ethernet header into ICMP request */
    memcpy(&req.eth_hdr, (void *)pkt_vaddr, ETH_HDR_LEN);

    /* Copy IP header into ICMP request */
    memcpy(&req.ip_hdr, (void *)ip_hdr, IPV4_HDR_LEN_MIN);

    /* Copy payload */
    uint8_t *payload_data = (uint8_t *)(pkt_vaddr + ICMP_ECHO_OFFSET + sizeof(icmp_echo_t));
    memcpy(req.echo.data, payload_data, payload_len);

    return fw_enqueue(icmp_queue, &req);
}

/**
 * Enqueue an ICMP redirect request.
 *
 * @param icmp_queue Pointer to the ICMP queue to enqueue the request.
 * @param code ICMP redirect code (e.g., ICMP_REDIRECT_FOR_HOST).
 * @param pkt_vaddr Virtual address of the packet data.
 * @param gateway_ip IP address of the gateway to redirect to.
 *
 * @return 0 on success, non-zero on failure.
 */
static inline int icmp_enqueue_redirect(fw_queue_t *icmp_queue, uint8_t code, uintptr_t pkt_vaddr, uint32_t gateway_ip)
{
    icmp_req_t req = {0};
    req.type = ICMP_REDIRECT_MSG;
    req.code = code;

    /* Copy ethernet header into ICMP request */
    memcpy(&req.eth_hdr, (void *)pkt_vaddr, ETH_HDR_LEN);

    /* Copy IP header into ICMP request */
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
    memcpy(&req.ip_hdr, (void *)ip_hdr, IPV4_HDR_LEN_MIN);

    /* Set gateway IP and copy first bytes of data */
    req.redirect.gateway_ip = gateway_ip;
    uint16_t to_copy = MIN(FW_ICMP_SRC_DATA_LEN, htons(ip_hdr->tot_len) - IPV4_HDR_LEN_MIN);
    memcpy(req.redirect.data, (void *)(pkt_vaddr + IPV4_HDR_OFFSET + IPV4_HDR_LEN_MIN), to_copy);

    return fw_enqueue(icmp_queue, &req);
}