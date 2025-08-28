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
#include <sddf/network/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/icmp.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".fw_icmp_module_config"))) fw_icmp_module_config_t icmp_config;
__attribute__((__section__(".ext_net_client_config"))) net_client_config_t ext_net_config;
__attribute__((__section__(".int_net_client_config"))) net_client_config_t int_net_config;

net_client_config_t *net_configs[FW_NUM_INTERFACES] = {&ext_net_config, &int_net_config};

net_queue_handle_t net_queue[FW_NUM_INTERFACES];
fw_queue_t icmp_queue[FW_NUM_INTERFACES];

static void generate_icmp(void)
{
    bool transmitted[FW_NUM_INTERFACES] = {false};
    for (uint8_t out_int = 0; out_int < icmp_config.num_interfaces; out_int++) {
        while (!fw_queue_empty(&icmp_queue[out_int]) && !net_queue_empty_free(&net_queue[out_int])) {
            icmp_req_t req = {0};
            int err = fw_dequeue(&icmp_queue[out_int], &req);
            assert(!err);

            net_buff_desc_t buffer = {};
            err = net_dequeue_free(&net_queue[out_int], &buffer);
            assert(!err);

            icmp_packet_t *icmp_resp = (icmp_packet_t *) (net_configs[out_int]->tx_data.vaddr + buffer.io_or_offset);
            /* Construct ethernet header */
            memcpy(&icmp_resp->ethdst_addr, &req.hdr.ethsrc_addr, ETH_HWADDR_LEN);
            memcpy(&icmp_resp->ethsrc_addr, &req.hdr.ethdst_addr, ETH_HWADDR_LEN);
            icmp_resp->eth_type = HTONS(ETH_TYPE_IP);
            
            /* Construct IP packet */
            icmp_resp->ihl_version = (4 << 4) | (5);
            /* The differentiated services code 48 is for network control traffic. */
            icmp_resp->tos = 48;

            /**
             * Hardcode the total length of a destination unreachable packet
             * here. The total length of the IP packet and the ICMP packet,
             * therefore we subtract the size of the ethernet header.
             */
             icmp_resp->tot_len = HTONS(sizeof(icmp_packet_t) - sizeof(struct ethernet_header));

            /* Not fragmenting this IP packet. */
            icmp_resp->id = HTONS(0);

            /* 0x4000 sets the "Don't Fragment" Bit */
            icmp_resp->frag_off = HTONS(0x4000);

            /* Recommended inital value of ttl is 64 hops according to the TCP/IP spec. */
            icmp_resp->ttl = 64;
            icmp_resp->protocol = IPV4_PROTO_ICMP;
            icmp_resp->check = 0;

            /* Source of IP packet is the firewall */
            icmp_resp->src_ip = icmp_config.ips[out_int];
            icmp_resp->dst_ip = req.hdr.src_ip;
            icmp_resp->type = req.type;
            icmp_resp->code = req.code;

            /* Set checksum to 0 for hardware calculation */
            icmp_resp->checksum = 0;

            /* IP header starts from ihl_version field */
            memcpy(&icmp_resp->old_ip_hdr, &req.hdr.ihl_version, sizeof(ipv4hdr_t));
            memcpy(&icmp_resp->old_data, req.data, FW_ICMP_OLD_DATA_LEN);

            buffer.len = sizeof(icmp_packet_t);
            err = net_enqueue_active(&net_queue[out_int], buffer);
            transmitted[out_int] = true;
            assert(!err);

            if (FW_DEBUG_OUTPUT) {
                sddf_printf("ICMP module sending packet for ip %s with type %u, code %u\n",
                    ipaddr_to_string(icmp_resp->dst_ip, ip_addr_buf0), icmp_resp->type, icmp_resp->code);
            }
        }
    }

    for (uint8_t out_int = 0; out_int < icmp_config.num_interfaces; out_int++) { 
        if (transmitted[out_int]) {
            microkit_deferred_notify(net_configs[out_int]->tx.id);
        }
    }
}

void init(void)
{
    for (int i = 0; i < icmp_config.num_interfaces; i++) {
        /* Setup the queue with the router. */
        fw_queue_init(&icmp_queue[i], icmp_config.routers[i].queue.vaddr,
            sizeof(icmp_req_t), icmp_config.routers[i].capacity);

        /* Setup transmit queues with the transmit virtualisers. */
        net_queue_init(&net_queue[i], net_configs[i]->tx.free_queue.vaddr,
            net_configs[i]->tx.active_queue.vaddr, net_configs[i]->tx.num_buffers);
        net_buffers_init(&net_queue[i], 0);
    }
}

void notified(microkit_channel ch)
{
    generate_icmp();
}
