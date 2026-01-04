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

            /* Currently we support destination unreachable and echo reply ICMP traffic */
            if (req.type != ICMP_DEST_UNREACHABLE && req.type != ICMP_ECHO_REPLY) {
                sddf_printf("ICMP module: Interface %u requested unsupported ICMP type %u!\n", out_int, req.type);
                continue;
            }

            net_buff_desc_t buffer = {};
            err = net_dequeue_free(&net_queue[out_int], &buffer);
            assert(!err);

            uintptr_t pkt_vaddr = (uintptr_t)(net_configs[out_int]->tx_data.vaddr + buffer.io_or_offset);

            /* Construct ethernet header */
            eth_hdr_t *eth_hdr = (eth_hdr_t *)pkt_vaddr;
            if (req.type == ICMP_ECHO_REPLY) {
                /* For echo reply, router already prepared the correct MAC addresses */
                memcpy(&eth_hdr->ethdst_addr, &req.eth_hdr.ethdst_addr, ETH_HWADDR_LEN);
                memcpy(&eth_hdr->ethsrc_addr, &req.eth_hdr.ethsrc_addr, ETH_HWADDR_LEN);
            } else {
                /* For other ICMP types (e.g., dest unreachable), swap addresses */
                memcpy(&eth_hdr->ethdst_addr, &req.eth_hdr.ethsrc_addr, ETH_HWADDR_LEN);
                memcpy(&eth_hdr->ethsrc_addr, &req.eth_hdr.ethdst_addr, ETH_HWADDR_LEN);
            }
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
            ip_hdr->src_ip = icmp_config.ips[out_int];
            /* Destination depends on ICMP type - set in switch below */

            /* Construct ICMP packet */
            icmp_hdr_t *icmp_hdr = (icmp_hdr_t *)(pkt_vaddr + ICMP_HDR_OFFSET);
            icmp_hdr->type = req.type;
            icmp_hdr->code = req.code;

            /* Handle each ICMP type separately */
            switch (req.type) {
                case ICMP_ECHO_REPLY: {
                    /* Destination is the original sender (already swapped in req) */
                    ip_hdr->dst_ip = req.ip_hdr.dst_ip;

                    /* Total length of ICMP echo reply IP packet */
                    uint16_t icmp_payload_len = req.payload_len;
                    ip_hdr->tot_len = htons(IPV4_HDR_LEN_MIN + ICMP_COMMON_HDR_LEN + 4 + icmp_payload_len);

                    /* Construct ICMP echo reply: 4 bytes (id + seq) + payload */
                    uint8_t *icmp_data = (uint8_t *)(pkt_vaddr + ICMP_HDR_OFFSET + ICMP_COMMON_HDR_LEN);
                    /* Echo identifier (network byte order) */
                    icmp_data[0] = (req.echo_id >> 8) & 0xFF;
                    icmp_data[1] = req.echo_id & 0xFF;
                    /* Echo sequence number (network byte order) */
                    icmp_data[2] = (req.echo_seq >> 8) & 0xFF;
                    icmp_data[3] = req.echo_seq & 0xFF;
                    /* Copy payload */
                    memcpy(icmp_data + 4, req.data, icmp_payload_len);

                    if (FW_DEBUG_OUTPUT) {
                        sddf_printf("ICMP module: echo reply to %s id=%u seq=%u len=%u\n",
                            ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0),
                            req.echo_id, req.echo_seq, icmp_payload_len);
                    }
                    break;
                }

                case ICMP_DEST_UNREACHABLE:
                    /* Destination is original packet's source */
                    ip_hdr->dst_ip = req.ip_hdr.src_ip;

                    /* Total length of ICMP destination unreachable IP packet */
                    // TODO: Should this be less if the source packet did not
                    // contain 8 bytes of data?
                    ip_hdr->tot_len = htons(IPV4_HDR_LEN_MIN + ICMP_DEST_LEN);

                    /* Construct ICMP destination unreachable packet */
                    icmp_dest_t *icmp_dest = (icmp_dest_t *)(pkt_vaddr + ICMP_DEST_OFFSET);

                    /* Unused must be set to 0, as well as optional fields we
                    are not currently using */
                    icmp_dest->unused = 0;
                    icmp_dest->len = 0;
                    icmp_dest->nexthop_mtu = 0;

                    /* Copy IP header */
                    memcpy(&icmp_dest->ip_hdr, &req.ip_hdr, IPV4_HDR_LEN_MIN);

                    /* Copy first bytes of data if applicable */
                    // TODO: If the source packet did not contain 8 bytes of
                    // data, should the rest be zero-filled?
                    uint16_t to_copy = MIN(FW_ICMP_SRC_DATA_LEN, htons(req.ip_hdr.tot_len) - IPV4_HDR_LEN_MIN);
                    memcpy(&icmp_dest->data, req.data, to_copy);
                    break;

                default:
                    sddf_printf("ICMP module tried to construct an unsupported ICMP type %u packet for interface %u!\n",
                                req.type, out_int);
                    break;
            }

            /* Set checksum to 0 and leave calculation to hardware. If this is
            no supported, calculate IP and ICMP checksums here */
            icmp_hdr->check = 0;
            ip_hdr->check = 0;

            #ifndef NETWORK_HW_HAS_CHECKSUM
            /* ICMP checksum is calculated over entire ICMP packet */
            icmp_hdr->check = fw_internet_checksum(icmp_hdr, htons(ip_hdr->tot_len) - IPV4_HDR_LEN_MIN);
            /* IP checksum is calculated only over IP header */
            ip_hdr->check = fw_internet_checksum(ip_hdr, IPV4_HDR_LEN_MIN);
            #endif

            buffer.len = htons(ip_hdr->tot_len) + ETH_HDR_LEN;
            err = net_enqueue_active(&net_queue[out_int], buffer);
            transmitted[out_int] = true;
            assert(!err);

            if (FW_DEBUG_OUTPUT) {
                sddf_printf("ICMP module sending packet for ip %s with type %u, code %u\n",
                    ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0), icmp_hdr->type, icmp_hdr->code);
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
