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
#include <sddf/util/cache.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/network/util.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/icmp_queue.h>
#include <lions/firewall/common.h>


__attribute__((__section__(".fw_icmp_module_config"))) fw_icmp_module_config_t icmp_config;

__attribute__((__section__(".net1_client_config"))) net_client_config_t net1_config;

__attribute__((__section__(".net2_client_config"))) net_client_config_t net2_config;

typedef struct state {
    // External network
    net_queue_handle_t net1_queue;
    // Internal Network
    net_queue_handle_t net2_queue;
    // External router queue
    icmp_queue_handle_t icmp_queue_router1;
    // Internal router queue
    icmp_queue_handle_t icmp_queue_router2;
} state_t;

state_t state;

void generate_icmp(int out_net)
{
    bool transmitted = false;
    net_queue_handle_t *curr_net;
    net_client_config_t *curr_config;

    if (out_net == 1) {
        curr_net = &state.net1_queue;
        curr_config = &net1_config;
    } else {
        curr_net = &state.net2_queue;
        curr_config = &net2_config;
    }

    for (int router = 0; router < 2; router++) {
        icmp_queue_handle_t *curr_icmp_queue;

        if (router == 0) {
            curr_icmp_queue = &state.icmp_queue_router1;
        } else {
            curr_icmp_queue = &state.icmp_queue_router2;
        }

        while (!icmp_queue_empty(curr_icmp_queue) && !net_queue_empty_free(curr_net)) {
            icmp_req_t req = {0};
            int err = icmp_dequeue(curr_icmp_queue, &req);
            assert(!err);

            net_buff_desc_t buffer = {};
            err = net_dequeue_free(curr_net, &buffer);
            assert(!err);

            icmphdr_t *icmp_resp = (icmphdr_t *) (curr_config->tx_data.vaddr + buffer.io_or_offset);
            // Construct the IP header for the response.
            sddf_memcpy(&icmp_resp->ethdst_addr, &req.old_hdr.ethsrc_addr, ETH_HWADDR_LEN);
            sddf_memcpy(&icmp_resp->ethsrc_addr, &req.old_hdr.ethdst_addr, ETH_HWADDR_LEN);
            icmp_resp->eth_type = HTONS(ETH_TYPE_IP);
            icmp_resp->ihl_version = (4 << 4) | (5);
            // The differentiated services code 48 is for network control traffic.
            icmp_resp->tos = 48;

            // Hardcode the total length of a destination unreachable packet here.
            // The total length of the IP packet and the ICMP packet, therefore we
            // subtract the size of the ethernet header.
            icmp_resp->tot_len = HTONS(sizeof(icmphdr_t) - sizeof(struct ethernet_header));

            // Not fragmenting this IP packet.
            icmp_resp->id = HTONS(0);

            // 0x4000 sets the "Don't Fragment" Bit
            icmp_resp->frag_off = HTONS(0x4000);

            // Recommended inital value of ttl is 64 hops according to the TCP/IP spec.
            // @kwinter: Alot of places use 255 TTL as the default?
            icmp_resp->ttl = 64;
            icmp_resp->protocol = IPV4_PROTO_ICMP;
            icmp_resp->check = 0;

            icmp_resp->src_ip = req.ip;
            icmp_resp->dst_ip = req.old_hdr.src_ip;
            icmp_resp->type = req.type;
            icmp_resp->code = req.code;

            // Checksum needs to be 0 for currect checksum calculation.
            icmp_resp->checksum = 0;

            // Strip away the ethernet header from the old packet.
            sddf_memcpy(&icmp_resp->old_ip_hdr, &req.old_hdr.ihl_version, sizeof(ipv4_packet_no_enet_t));
            sddf_memcpy(&icmp_resp->old_data, &req.old_data, 8);

            buffer.len = (sizeof(icmphdr_t));
            cache_clean((unsigned long)icmp_resp, (unsigned long)(icmp_resp + sizeof(icmphdr_t)));
            err = net_enqueue_active(curr_net, buffer);
            transmitted = true;
            assert(!err);
        }
    }

    if (transmitted) {
        if (out_net == 1) {
            microkit_deferred_notify(net1_config.tx.id);
        } else {
            microkit_deferred_notify(net2_config.tx.id);
        }
    }
}

void init(void)
{
    /* Setup the queue with the router. */
    icmp_queue_init(&state.icmp_queue_router1, icmp_config.router1_conn.queue.vaddr, icmp_config.router1_conn.capacity);
    icmp_queue_init(&state.icmp_queue_router2, icmp_config.router2_conn.queue.vaddr, icmp_config.router2_conn.capacity);

    /* Setup the queue with the transmit virtualisers. */
    net_queue_init(&state.net1_queue, net1_config.tx.free_queue.vaddr, net1_config.tx.active_queue.vaddr,
        net1_config.tx.num_buffers);
    net_buffers_init(&state.net1_queue, 0);

    net_queue_init(&state.net2_queue, net2_config.tx.free_queue.vaddr, net2_config.tx.active_queue.vaddr,
        net2_config.tx.num_buffers);
    net_buffers_init(&state.net2_queue, 0);
}

void notified(microkit_channel ch)
{
    if (ch == icmp_config.router1_conn.ch) {
        // Set out net argument to network 1 - this ICMP packet will
        // go out to the external network.
        generate_icmp(1);
    } else if (ch == icmp_config.router2_conn.ch) {
        // Set out net argument to network 2 - this ICMP packet will
        // go out to the internal network.
        generate_icmp(2);
    }
}