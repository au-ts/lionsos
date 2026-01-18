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
/* Table storing ephemeral ports of the mirror nat */
fw_nat_port_table_t *mirror_port_table;

fw_nat_interface_config_t nat_interface_config;
fw_nat_interface_config_t mirror_nat_interface_config;

static void translate(void)
{
    net_buff_desc_t buffer;

    while (!fw_queue_empty(&filter_queue)) {
        /* Incoming packet from filter */
        int err = fw_dequeue(&filter_queue, &buffer);
        assert(!err);

        uintptr_t pkt_vaddr = data_vaddr + buffer.io_or_offset;
        ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
        udp_hdr_t *udp_hdr = (udp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));


        if (FW_DEBUG_OUTPUT) {
            sddf_dprintf("%sUDP NAT LOG: src = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->src_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->src_port));
            sddf_dprintf("%sUDP NAT LOG: dst = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->dst_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->dst_port));
        }

        if (ip_hdr->dst_ip == mirror_nat_interface_config.snat) {
            if ((htons(udp_hdr->dst_port) >= mirror_nat_interface_config.base_port)
                && (htons(udp_hdr->dst_port) < mirror_nat_interface_config.base_port + mirror_port_table->size)) {
                sddf_dprintf("%sUDP NAT LOG: returning traffic detected\n", fw_frmt_str[nat_config.interface]);

                fw_nat_port_mapping_t original = mirror_port_table->mappings[htons(udp_hdr->dst_port) -
                                                                             mirror_nat_interface_config.base_port];

                udp_hdr->dst_port = original.src_port;
                ip_hdr->dst_ip = original.src_ip;
                ip_hdr->check = 0;
                udp_hdr->check = 0;
            }
        }

        if (nat_interface_config.snat) {
            uint16_t ephemeral_port = 0;

            /* Search for an existing mapping */
            for (uint16_t i = 0; i < port_table->size; i++) {
                if (port_table->mappings[i].src_ip == ip_hdr->src_ip && port_table->mappings[i].src_port == udp_hdr->src_port) {
                    ephemeral_port = nat_interface_config.base_port + i;
                    continue;
                }
            }

            /* Assign new ephemeral port */
            if (!ephemeral_port && port_table->size < nat_interface_config.ports_capacity) {
                port_table->mappings[port_table->size].src_port = udp_hdr->src_port;
                port_table->mappings[port_table->size].src_ip = ip_hdr->src_ip;

                ephemeral_port = nat_interface_config.base_port + port_table->size;

                port_table->size++;
            }

            if (ephemeral_port) {
                ip_hdr->src_ip = nat_interface_config.snat;
                ip_hdr->check = 0;

                udp_hdr->src_port = htons(ephemeral_port);
                udp_hdr->check = 0;

                if (FW_DEBUG_OUTPUT) {
                    sddf_dprintf("%sUDP NAT LOG: translated to %s:%u\n",
                                 fw_frmt_str[nat_config.interface],
                                 ipaddr_to_string(nat_interface_config.snat, ip_addr_buf0),
                                 htons(udp_hdr->src_port));
                }
            } else {
                sddf_dprintf("%sUDP NAT LOG: ephemeral ports ran out!\n", fw_frmt_str[nat_config.interface]);
            }
        } else {
            if (FW_DEBUG_OUTPUT) {
                sddf_dprintf("%sUDP NAT LOG: NAT disabled on this interface\n", fw_frmt_str[nat_config.interface]);
            }
        }

        if (FW_DEBUG_OUTPUT) {
            sddf_dprintf("%sUDP NAT LOG: src = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->src_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->src_port));
            sddf_dprintf("%sUDP NAT LOG: dst = %s:%u\n", fw_frmt_str[nat_config.interface], ipaddr_to_string(ip_hdr->dst_ip,
                                                                                                             ip_addr_buf0), htons(udp_hdr->dst_port));
        }

        /* Send packet out to router */
        fw_enqueue(&router_queue, &buffer);
        assert(!err);

        microkit_notify(nat_config.router.ch);
    }
}

void notified(microkit_channel ch)
{
    if (ch == nat_config.filter.ch) {
        translate();
    } else {
        sddf_dprintf("%sUDP NAT LOG: Received notification on unknown channel: %d!\n", fw_frmt_str[nat_config.interface],
                     ch);
    }
}

void init(void)
{
    data_vaddr = (uintptr_t)nat_config.data.region.vaddr;

    nat_interface_config = nat_config.interfaces[nat_config.interface];
    mirror_nat_interface_config = nat_config.interfaces[nat_config.interface == 0 ? 1 : 0];

    port_table = (fw_nat_port_table_t*)nat_interface_config.port_table.vaddr;
    mirror_port_table = (fw_nat_port_table_t*)mirror_nat_interface_config.port_table.vaddr;

    fw_queue_init(&router_queue, nat_config.router.queue.vaddr,
                  sizeof(net_buff_desc_t),  nat_config.router.capacity);

    fw_queue_init(&filter_queue, nat_config.filter.queue.vaddr,
                  sizeof(net_buff_desc_t),  nat_config.filter.capacity);

    if (FW_DEBUG_OUTPUT) {
        sddf_dprintf("%sUDP NAT LOG: base port: %u\ncapacity: %u\nsnat: %u\n", fw_frmt_str[nat_config.interface],
                     nat_interface_config.base_port,
                     nat_interface_config.ports_capacity,
                     nat_interface_config.snat
                    );
    }
}
