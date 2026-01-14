/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sddf/util/util.h>
#include <lions/firewall/icmp.h>
#include <lions/firewall/queue.h>

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
