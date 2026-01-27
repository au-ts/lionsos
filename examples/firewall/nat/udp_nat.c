/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "microkit.h"
#include <stdbool.h>
#include <stdint.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/nat.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/udp.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".fw_nat_config"))) fw_nat_config_t nat_config;

/* Incoming packets from filter */
fw_queue_t filter_queue;
/* Outgoing packets to router */
fw_queue_t router_queue;
/* Virtual address of rx buffer data region */
uintptr_t data_vaddr;
/* Table storing ephemeral ports */
fw_nat_port_table_t *port_table;

fw_nat_interface_config_t nat_interface_config;

static void log_packet(ipv4_hdr_t *ip_hdr, udp_hdr_t *udp_hdr)
{
    if (FW_DEBUG_OUTPUT) {
        sddf_printf("%sUDP NAT LOG: src = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->src_ip,
                                                                                                        ip_addr_buf0), htons(udp_hdr->src_port));
        sddf_printf("%sUDP NAT LOG: dst = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->dst_ip,
                                                                                                        ip_addr_buf0), htons(udp_hdr->dst_port));
    }
}

static void translate(void)
{
    net_buff_desc_t buffer;
    bool transmitted = false;

    while (!fw_queue_empty(&filter_queue)) {
        /* Incoming packet from filter */
        int err = fw_dequeue(&filter_queue, &buffer);
        assert(!err);

        uintptr_t pkt_vaddr = data_vaddr + buffer.io_or_offset;
        ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
        udp_hdr_t *udp_hdr = (udp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));

        log_packet(ip_hdr, udp_hdr);

        fw_nat_port_mapping_t *dst_mapping = fw_nat_translate_destination(nat_config.interfaces, ip_hdr->dst_ip,
                                                                          udp_hdr->dst_port);

        if (dst_mapping) {
            if (FW_DEBUG_OUTPUT) {
                sddf_printf("%sUDP NAT LOG: returning traffic detected\n", fw_frmt_str[nat_config.interface]);
            }
            udp_hdr->dst_port = dst_mapping->src_port;
            ip_hdr->dst_ip = dst_mapping->src_ip;
            ip_hdr->check = 0;
            udp_hdr->check = 0;
        }

        if (nat_interface_config.snat) {
            uint16_t ephemeral_port = fw_nat_find_ephemeral_port(nat_interface_config, port_table, ip_hdr->src_ip,
                                                                 udp_hdr->src_port);

            if (ephemeral_port) {
                ip_hdr->src_ip = nat_interface_config.snat;
                udp_hdr->src_port = ephemeral_port;
                udp_hdr->check = 0;
                ip_hdr->check = 0;

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("%sUDP NAT LOG: translated to %s:%u\n",
                                fw_frmt_str[nat_config.interface],
                                ipaddr_to_string(nat_interface_config.snat, ip_addr_buf0),
                                htons(udp_hdr->src_port));
                }
            } else {
                sddf_printf("%sUDP NAT LOG: ephemeral ports ran out!\n", fw_frmt_str[nat_config.interface]);
            }
        } else {
            if (FW_DEBUG_OUTPUT) {
                sddf_printf("%sUDP NAT LOG: NAT disabled on this interface\n", fw_frmt_str[nat_config.interface]);
            }
        }

        if (udp_hdr->check == 0) {
            udp_hdr->check = calculate_transport_checksum(udp_hdr, htons(ip_hdr->tot_len) - ipv4_header_length(ip_hdr),
                                                          IPV4_PROTO_UDP, ip_hdr->src_ip, ip_hdr->dst_ip);
        }

        log_packet(ip_hdr, udp_hdr);

        /* Send packet out to router */
        fw_enqueue(&router_queue, &buffer);
        assert(!err);

        transmitted = true;
    }

    if (transmitted) {
        microkit_notify(nat_config.router.ch);
    }
}

void notified(microkit_channel ch)
{
    if (ch == nat_config.filter.ch) {
        translate();
    } else {
        sddf_printf("%sUDP NAT LOG: Received notification on unknown channel: %d!\n", fw_frmt_str[nat_config.interface],
                    ch);
    }
}

void init(void)
{
    data_vaddr = (uintptr_t)nat_config.data.region.vaddr;

    nat_interface_config = nat_config.interfaces[nat_config.interface];

    port_table = (fw_nat_port_table_t*)nat_interface_config.port_table.vaddr;

    fw_queue_init(&router_queue, nat_config.router.queue.vaddr,
                  sizeof(net_buff_desc_t),  nat_config.router.capacity);

    fw_queue_init(&filter_queue, nat_config.filter.queue.vaddr,
                  sizeof(net_buff_desc_t),  nat_config.filter.capacity);

    if (FW_DEBUG_OUTPUT) {
        sddf_printf("%sUDP NAT LOG: base port: %u\ncapacity: %u\nsnat: %u\n", fw_frmt_str[nat_config.interface],
                    nat_interface_config.base_port,
                    nat_interface_config.ports_capacity,
                    nat_interface_config.snat
                   );
    }
}
