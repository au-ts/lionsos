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
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/nat.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".fw_nat_config"))) fw_nat_config_t nat_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

/* Incoming packets from filter */
fw_queue_t filter_queue;
/* Outgoing packets to router */
fw_queue_t router_queue;
/* Virtual address of rx buffer data region */
uintptr_t data_vaddr;
/* Table storing ephemeral ports */
fw_nat_port_table_t *port_table;
fw_nat_interface_config_t nat_interface_config;
/* Shared state with webserver */
fw_nat_webserver_state_t *webserver_state;

static void log_packet(ipv4_hdr_t *ip_hdr, uint16_t src_port, uint16_t dst_port)
{
    if (FW_DEBUG_OUTPUT)
    {
        sddf_printf("%s%s NAT LOG: src = %s:%u\n", fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                    ipaddr_to_string(ip_hdr->src_ip,
                                     ip_addr_buf0),
                    htons(src_port));
        sddf_printf("%s%s NAT LOG: dst = %s:%u\n", fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                    ipaddr_to_string(ip_hdr->dst_ip,
                                     ip_addr_buf0),
                    htons(dst_port));
    }
}

static void translate(void)
{
    net_buff_desc_t buffer;
    bool transmitted = false;

    /* This is a PPC (expensive), so it is called only once */
    uint64_t now = sddf_timer_time_now(timer_config.driver_id);

    while (!fw_queue_empty(&filter_queue))
    {
        /* Incoming packet from filter */
        int err = fw_dequeue(&filter_queue, &buffer);
        assert(!err);

        uintptr_t pkt_vaddr = data_vaddr + buffer.io_or_offset;
        ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
        char *transport_hdr = (char *)(pkt_vaddr + transport_layer_offset(ip_hdr));

        uint16_t *src_port = (uint16_t *)(transport_hdr + nat_config.src_port_off);
        uint16_t *dst_port = (uint16_t *)(transport_hdr + nat_config.dst_port_off);
        uint16_t *check = (uint16_t *)(transport_hdr + nat_config.check_off);

        uint32_t snat = webserver_state->interfaces[nat_config.interface].snat;

        bool recalculate_checksum = false;

        log_packet(ip_hdr, *src_port, *dst_port);

        /* Check for port forwarding rule first */
        fw_nat_port_forwarding_rule_t *fwd_rule = fw_nat_find_port_forwarding_rule(
            &webserver_state->port_forwarding,
            nat_config.protocol,
            *dst_port,
            nat_config.interface);

        if (fwd_rule && fwd_rule->is_valid)
        {
            if (FW_DEBUG_OUTPUT)
            {
                sddf_printf("%s%s NAT LOG: port forwarding rule matched: external:%u -> %s:%u\n",
                            fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                            htons(fwd_rule->external_port),
                            ipaddr_to_string(fwd_rule->internal_ip, ip_addr_buf0),
                            htons(fwd_rule->internal_port));
            }

            /* Apply port forwarding translation */
            *dst_port = fwd_rule->internal_port;
            ip_hdr->dst_ip = fwd_rule->internal_ip;
            ip_hdr->check = 0;
            recalculate_checksum = true;

            /* Create mapping for return traffic */
            uint16_t ephemeral_port = fw_nat_find_ephemeral_port(nat_interface_config, port_table, ip_hdr->src_ip,
                                                                 *src_port, now);
            if (ephemeral_port)
            {
                if (FW_DEBUG_OUTPUT)
                {
                    sddf_printf("%s%s NAT LOG: created mapping for forwarded connection: ephemeral port %u\n",
                                fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                                htons(ephemeral_port));
                }
            }
            else
            {
                sddf_printf("%s%s NAT LOG: could not create mapping for forwarded connection!\n",
                            fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol));
            }
        }
        else
        {
            /* Check for returning traffic from dynamic NAT */
            fw_nat_port_mapping_t *dst_mapping = fw_nat_translate_destination(nat_config.interfaces, *webserver_state, ip_hdr->dst_ip,
                                                                              *dst_port, now);

            if (dst_mapping)
            {
                if (FW_DEBUG_OUTPUT)
                {
                    sddf_printf("%s%s NAT LOG: returning traffic detected\n", fw_frmt_str[nat_config.interface],
                                ipv4_proto_name(nat_config.protocol));
                }
                *dst_port = dst_mapping->src_port;
                ip_hdr->dst_ip = dst_mapping->src_ip;
                ip_hdr->check = 0;

                recalculate_checksum = true;
            }
        }

        /* Apply source NAT for outgoing traffic */
        if (snat && ip_hdr->dst_ip != nat_interface_config.ip)
        {
            uint16_t ephemeral_port = fw_nat_find_ephemeral_port(nat_interface_config, port_table, ip_hdr->src_ip,
                                                                 *src_port, now);

            if (ephemeral_port)
            {
                ip_hdr->src_ip = snat;
                *src_port = ephemeral_port;
                ip_hdr->check = 0;
                recalculate_checksum = true;

                if (FW_DEBUG_OUTPUT)
                {
                    sddf_printf("%s%s NAT LOG: translated to %s:%u\n", fw_frmt_str[nat_config.interface],
                                ipv4_proto_name(nat_config.protocol),
                                ipaddr_to_string(snat, ip_addr_buf0),
                                htons(*src_port));
                }
            }
            else
            {
                sddf_printf("%s%s NAT LOG: ephemeral ports ran out!\n", fw_frmt_str[nat_config.interface],
                            ipv4_proto_name(nat_config.protocol));
            }
        }
        else
        {
            if (FW_DEBUG_OUTPUT)
            {
                sddf_printf("%s%s NAT LOG: NAT disabled on this interface\n", fw_frmt_str[nat_config.interface],
                            ipv4_proto_name(nat_config.protocol));
            }
        }

        if (recalculate_checksum && nat_config.check_enabled)
        {
            *check = 0;
            *check = calculate_transport_checksum(transport_hdr, htons(ip_hdr->tot_len) - ipv4_header_length(ip_hdr),
                                                  nat_config.protocol, ip_hdr->src_ip, ip_hdr->dst_ip);
        }

        log_packet(ip_hdr, *src_port, *dst_port);

        /* Send packet out to router */
        fw_enqueue(&router_queue, &buffer);
        assert(!err);

        transmitted = true;
    }

    if (transmitted)
    {
        microkit_notify(nat_config.router.ch);
    }
}

/* Port forwarding IPC message labels */
#define FW_NAT_ADD_PORT_FORWARDING 10
#define FW_NAT_DEL_PORT_FORWARDING 11
#define FW_NAT_LIST_PORT_FORWARDING 12

/* Port forwarding IPC message register indices */
#define NAT_ARG_PROTOCOL 0
#define NAT_ARG_EXTERNAL_PORT 1
#define NAT_ARG_INTERNAL_IP 2
#define NAT_ARG_INTERNAL_PORT 3
#define NAT_ARG_INTERFACE 4
#define NAT_ARG_INDEX 5

/* Port forwarding IPC return register indices */
#define NAT_RET_ERR 0
#define NAT_RET_NUM_RULES 1

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    switch (microkit_msginfo_get_label(msginfo))
    {
    case FW_NAT_ADD_PORT_FORWARDING:
    {
        uint8_t protocol = microkit_mr_get(NAT_ARG_PROTOCOL);
        uint16_t external_port = microkit_mr_get(NAT_ARG_EXTERNAL_PORT);
        uint32_t internal_ip = microkit_mr_get(NAT_ARG_INTERNAL_IP);
        uint16_t internal_port = microkit_mr_get(NAT_ARG_INTERNAL_PORT);
        uint8_t interface = microkit_mr_get(NAT_ARG_INTERFACE);

        int err = fw_nat_add_port_forwarding_rule(&webserver_state->port_forwarding,
                                                  protocol, external_port,
                                                  internal_ip, internal_port, interface);

        if (FW_DEBUG_OUTPUT)
        {
            if (err == 0)
            {
                sddf_printf("%s%s NAT LOG: added port forwarding rule: %s:%u -> %s:%u\n",
                            fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                            ipaddr_to_string(webserver_state->interfaces[interface].snat, ip_addr_buf0),
                            htons(external_port),
                            ipaddr_to_string(internal_ip, ip_addr_buf1),
                            htons(internal_port));
            }
            else
            {
                sddf_printf("%s%s NAT LOG: failed to add port forwarding rule: error %d\n",
                            fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol), err);
            }
        }

        microkit_mr_set(NAT_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FW_NAT_DEL_PORT_FORWARDING:
    {
        uint16_t index = microkit_mr_get(NAT_ARG_INDEX);

        int err = fw_nat_remove_port_forwarding_rule(&webserver_state->port_forwarding, index);

        if (FW_DEBUG_OUTPUT)
        {
            if (err == 0)
            {
                sddf_printf("%s%s NAT LOG: removed port forwarding rule at index %u\n",
                            fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol), index);
            }
            else
            {
                sddf_printf("%s%s NAT LOG: failed to remove port forwarding rule at index %u: error %d\n",
                            fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol), index, err);
            }
        }

        microkit_mr_set(NAT_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FW_NAT_LIST_PORT_FORWARDING:
    {
        uint16_t num_rules = webserver_state->port_forwarding.num_rules;

        if (FW_DEBUG_OUTPUT)
        {
            sddf_printf("%s%s NAT LOG: listing %u port forwarding rules\n",
                        fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol), num_rules);
        }

        microkit_mr_set(NAT_RET_NUM_RULES, num_rules);
        return microkit_msginfo_new(0, 2);
    }
    default:
        sddf_printf("%s%s NAT LOG: Unknown IPC request: %lu\n",
                    fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                    microkit_msginfo_get_label(msginfo));
        break;
    }

    return microkit_msginfo_new(0, 0);
}

void notified(microkit_channel ch)
{
    if (ch == nat_config.filter.ch)
    {
        translate();
    }
    else if (ch == timer_config.driver_id)
    {
        uint64_t now = sddf_timer_time_now(timer_config.driver_id);
        fw_nat_free_expired_mappings(nat_interface_config, port_table, webserver_state->timeout, now);
        sddf_timer_set_timeout(timer_config.driver_id, NAT_TIMEOUT_INTERVAL_NS);
    }
    else
    {
        sddf_printf("%s%s NAT LOG: Received notification on unknown channel: %d!\n",
                    fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                    ch);
    }
}

void init(void)
{
    data_vaddr = (uintptr_t)nat_config.data.region.vaddr;

    nat_interface_config = nat_config.interfaces[nat_config.interface];

    port_table = (fw_nat_port_table_t *)nat_interface_config.port_table.vaddr;

    webserver_state = (fw_nat_webserver_state_t *)nat_config.webserver.vaddr;

    fw_queue_init(&router_queue, nat_config.router.queue.vaddr,
                  sizeof(net_buff_desc_t), nat_config.router.capacity);

    fw_queue_init(&filter_queue, nat_config.filter.queue.vaddr,
                  sizeof(net_buff_desc_t), nat_config.filter.capacity);

    if (FW_DEBUG_OUTPUT)
    {
        sddf_printf("%s%s NAT LOG: base port: %u\ncapacity: %u\n",
                    fw_frmt_str[nat_config.interface], ipv4_proto_name(nat_config.protocol),
                    nat_interface_config.base_port,
                    nat_interface_config.ports_capacity);
    }

    /* Set first tick */
    sddf_timer_set_timeout(timer_config.driver_id, NAT_TIMEOUT_INTERVAL_NS);
}
