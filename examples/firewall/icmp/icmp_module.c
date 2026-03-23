/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/ethernet.h>
#include <lions/firewall/icmp.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/routing.h>

__attribute__((__section__(".fw_icmp_module_config"))) fw_icmp_module_config_t icmp_config;
__attribute__((__section__(".net_config_0"))) net_client_config_t net_config_0;
__attribute__((__section__(".net_config_1"))) net_client_config_t net_config_1;

net_client_config_t *net_configs[FW_MAX_INTERFACES] = { &net_config_0, &net_config_1 };

net_queue_handle_t net_queue[FW_MAX_INTERFACES];
fw_queue_t router_icmp_queue;
fw_queue_t filter_icmp_queue[FW_MAX_INTERFACES][FW_MAX_FILTERS];

static bool process_icmp_request(icmp_req_t *req, bool *transmitted)
{
    if (req->type != ICMP_DEST_UNREACHABLE && req->type != ICMP_ECHO_REPLY && req->type != ICMP_TTL_EXCEED) {
        sddf_printf("ICMP MODULE LOG: unsupported ICMP type %u!\n", req->type);
        return false;
    }

    uint8_t out_int = req->out_interface;
    if (net_queue_empty_free(&net_queue[out_int])) {
        return false;
    }

    net_buff_desc_t buffer = {};
    int err = net_dequeue_free(&net_queue[out_int], &buffer);
    assert(!err);

    uintptr_t pkt_vaddr = (uintptr_t)(net_configs[out_int]->tx_data.vaddr + buffer.io_or_offset);

    /* Construct ethernet header */
    eth_hdr_t *eth_hdr = (eth_hdr_t *)pkt_vaddr;
    memcpy(&eth_hdr->ethdst_addr, &req->eth_hdr.ethsrc_addr, ETH_HWADDR_LEN);
    memcpy(&eth_hdr->ethsrc_addr, &icmp_config.interfaces[out_int].mac_addr, ETH_HWADDR_LEN);
    eth_hdr->ethtype = htons(ETH_TYPE_IP);

    /* Construct IP packet */
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
    ip_hdr->version = 4;
    ip_hdr->ihl = IPV4_HDR_LEN_MIN / 4;
    ip_hdr->dscp = IPV4_DSCP_NET_CTRL;
    ip_hdr->ecn = 0;

    /* Not fragmenting this IP packet, set all other fields to 0 */
    ip_hdr->id = 0;
    ip_hdr->frag_offset1 = 0;
    ip_hdr->frag_offset2 = 0;
    ip_hdr->more_frag = 0;
    ip_hdr->no_frag = 1;
    ip_hdr->reserved = 0;

    /* Recommended inital value of ttl is 64 hops according to the TCP/IP spec */
    ip_hdr->ttl = 64;
    ip_hdr->protocol = IPV4_PROTO_ICMP;

    /* Source of IP packet is the firewall */
    ip_hdr->src_ip = icmp_config.interfaces[out_int].ip;
    /* Destination depends on ICMP type - set in switch below */

    /* Construct ICMP packet */
    icmp_hdr_t *icmp_hdr = (icmp_hdr_t *)(pkt_vaddr + ICMP_HDR_OFFSET);
    icmp_hdr->type = req->type;
    icmp_hdr->code = req->code;

    uint16_t to_copy = MIN(FW_ICMP_SRC_DATA_LEN, ntohs(req->ip_hdr.tot_len) - IPV4_HDR_LEN_MIN);

    /* Handle each ICMP type separately */
    switch (req->type) {
    case ICMP_ECHO_REPLY: {
        /* Destination is the sender */
        ip_hdr->dst_ip = req->ip_hdr.src_ip;

        /* Total length of ICMP destination unreachable IP packet */
        uint16_t icmp_total_len = (uint16_t)(ICMP_COMMON_HDR_LEN + sizeof(icmp_echo_t) + req->echo.payload_len);
        ip_hdr->tot_len = htons(IPV4_HDR_LEN_MIN + icmp_total_len);

        /* Construct ICMP echo reply: 4 bytes (id + seq) */
        icmp_echo_t *icmp_echo = (icmp_echo_t *)(pkt_vaddr + ICMP_PAYLOAD_OFFSET);

        /* Set Echo-specific fields from the request */
        icmp_echo->id = htons(req->echo.echo_id);
        icmp_echo->seq = htons(req->echo.echo_seq);

        /* Copy the actual Echo payload data (the 'ping' data) */
        memcpy(icmp_echo->data, req->echo.data, req->echo.payload_len);
        break;
    }

    case ICMP_DEST_UNREACHABLE:
        /* Destination is original packet's source */
        ip_hdr->dst_ip = req->ip_hdr.src_ip;

        /* Total length of ICMP destination unreachable IP packet */
        ip_hdr->tot_len = htons(IPV4_HDR_LEN_MIN + ICMP_DEST_LEN);

        /* Construct ICMP destination unreachable packet */
        icmp_dest_t *icmp_dest = (icmp_dest_t *)(pkt_vaddr + ICMP_PAYLOAD_OFFSET);

        /* Unused must be set to 0, as well as optional fields we are not currently using */
        icmp_dest->unused = 0;
        icmp_dest->len = 0;
        icmp_dest->nexthop_mtu = 0;

        /* Copy IP header */
        memcpy(&icmp_dest->ip_hdr, &req->ip_hdr, IPV4_HDR_LEN_MIN);

        /* Copy first bytes of data if applicable */
        memcpy(&icmp_dest->data, req->dest.data, to_copy);
        break;
    case ICMP_TTL_EXCEED:
        /* Destination is original packet's source */
        ip_hdr->dst_ip = req->ip_hdr.src_ip;

        /* Total length of ICMP time exceeded IP packet */
        ip_hdr->tot_len = htons(IPV4_HDR_LEN_MIN + ICMP_TIME_EXCEEDED_LEN);

        /* Construct ICMP time exceeded packet */
        icmp_time_exceeded_t *icmp_time_exceeded = (icmp_time_exceeded_t *)(pkt_vaddr + ICMP_PAYLOAD_OFFSET);

        /* Unused must be set to 0 */
        icmp_time_exceeded->unused = 0;

        /* Copy IP header */
        memcpy(&icmp_time_exceeded->ip_hdr, &req->ip_hdr, IPV4_HDR_LEN_MIN);

        /* Copy first bytes of data if applicable */
        memcpy(&icmp_time_exceeded->data, req->time_exceeded.data, to_copy);
        break;
    case ICMP_REDIRECT_MSG:
        /* Destination is original packet's source */
        ip_hdr->dst_ip = req->ip_hdr.src_ip;

        /* Total length of ICMP redicrt IP packet */
        ip_hdr->tot_len = htons(IPV4_HDR_LEN_MIN + ICMP_REDIRECT_LEN);

        /* Construct ICMP redirect packet */
        icmp_redirect_t *icmp_redirect = (icmp_redirect_t *)(pkt_vaddr + ICMP_PAYLOAD_OFFSET);

        /* Set the gateway IP address*/
        icmp_redirect->gateway_ip = req->redirect.gateway_ip;

        /* Copy IP header */
        memcpy(&icmp_redirect->ip_hdr, &req->ip_hdr, IPV4_HDR_LEN_MIN);
        /* Copy first bytes of data if applicable */
        memcpy(&icmp_redirect->data, req->redirect.data, to_copy);
        break;
    default:
        return false;
    }

    /* Set checksum to 0 and leave calculation to hardware. If this is not supported, calculate IP and ICMP checksums here */
    icmp_hdr->check = 0;
    ip_hdr->check = 0;

    uint16_t ip_tot_len = ntohs(ip_hdr->tot_len);

#ifndef NETWORK_HW_HAS_CHECKSUM
    /* ICMP checksum is calculated over entire ICMP packet */
    icmp_hdr->check = fw_internet_checksum(icmp_hdr, ip_tot_len - IPV4_HDR_LEN_MIN);
    /* IP checksum is calculated only over IP header */
    ip_hdr->check = fw_internet_checksum(ip_hdr, IPV4_HDR_LEN_MIN);
#endif

    buffer.len = ip_tot_len + ETH_HDR_LEN;
    err = net_enqueue_active(&net_queue[out_int], buffer);
    transmitted[out_int] = true;
    assert(!err);

    if (FW_DEBUG_OUTPUT) {
        sddf_printf("ICMP MODULE LOG: sending packet for ip %s with type %u, code %u\n",
                    ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0), icmp_hdr->type, icmp_hdr->code);
    }

    return true;
}

static void generate_icmp(void)
{
    bool transmitted[FW_MAX_INTERFACES] = { false };

    /* Process ICMP requests from filters */
    for (uint8_t iface = 0; iface < icmp_config.num_interfaces; iface++) {
        for (uint8_t filter_idx = 0; filter_idx < icmp_config.interfaces[iface].num_filters; filter_idx++) {
            while (!fw_queue_empty(&filter_icmp_queue[iface][filter_idx])) {
                icmp_req_t req = { 0 };
                int err = fw_dequeue(&filter_icmp_queue[iface][filter_idx], &req);
                assert(!err);

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("ICMP MODULE LOG: processing filter %u ICMP request type %u code %u on interface %u\n",
                                filter_idx, req.type, req.code, req.out_interface);
                }

                process_icmp_request(&req, transmitted);
            }
        }
    }

    /* Process ICMP requests from router */
    while (!fw_queue_empty(&router_icmp_queue)) {
        icmp_req_t req = { 0 };
        int err = fw_dequeue(&router_icmp_queue, &req);
        assert(!err);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("ICMP MODULE LOG: processing router ICMP request type %u code %u using interface %u\n", req.type,
                        req.code, req.out_interface);
        }

        process_icmp_request(&req, transmitted);
    }

    for (uint8_t out_int = 0; out_int < icmp_config.num_interfaces; out_int++) {
        if (transmitted[out_int]) {
            microkit_deferred_notify(net_configs[out_int]->tx.id);
        }
    }
}

void init(void)
{
    /* Setup the queue with the router. */
    fw_queue_init(&router_icmp_queue, icmp_config.router.queue.vaddr, sizeof(icmp_req_t), icmp_config.router.capacity);

    for (int out = 0; out < icmp_config.num_interfaces; out++) {
        /* Setup transmit queues with the transmit virtualisers. */
        net_queue_init(&net_queue[out], net_configs[out]->tx.free_queue.vaddr, net_configs[out]->tx.active_queue.vaddr,
                       net_configs[out]->tx.num_buffers);
        net_buffers_init(&net_queue[out], 0);

        /* Setup queues with filters */
        for (int i = 0; i < icmp_config.interfaces[out].num_filters; i++) {
            fw_queue_init(&filter_icmp_queue[out][i], icmp_config.interfaces[out].filters[i].queue.vaddr,
                          sizeof(icmp_req_t), icmp_config.interfaces[out].filters[i].capacity);
        }
    }
}

void notified(microkit_channel ch)
{
    generate_icmp();
}
