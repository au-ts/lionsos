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
#include <lions/firewall/nat_module.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/common.h>

/**
 * Initialise the NAT module
 */
int nat_module_init(nat_module_t *nat,
                    uint8_t interface,
                    uint8_t protocol,
                    fw_nat_interface_config_t *interface_config,
                    fw_nat_port_table_t *port_table,
                    size_t src_port_off,
                    size_t dst_port_off,
                    size_t check_off,
                    bool check_enabled)
{
    if (!nat || !interface_config || !port_table)
    {
        return NAT_FAILURE;
    }

    nat->interface = interface;
    nat->protocol = protocol;
    nat->nat_enabled = false;
    nat->interface_config = interface_config;
    nat->port_table = port_table;
    nat->src_port_off = src_port_off;
    nat->dst_port_off = dst_port_off;
    nat->check_off = check_off;
    nat->check_enabled = check_enabled;

    /* Initialise statistics */
    nat->translations_performed = 0;
    nat->translations_failed = 0;
    nat->dnat_hits = 0;
    nat->snat_hits = 0;

    if (FW_DEBUG_OUTPUT)
    {
        sddf_printf("%s%s NAT Module: initialized, base port = %u, capacity = %u\n",
                    "iface",
                    "protocol",
                    interface_config->base_port,
                    interface_config->ports_capacity);
    }

    return NAT_SUCCESS;
}

/**
 * Translate a packet using NAT
 */
int nat_module_translate(nat_module_t *nat,
                         uintptr_t pkt_vaddr,
                         net_buff_desc_t *buffer,
                         bool do_dnat)
{
    if (!nat || !pkt_vaddr)
    {
        return NAT_INVALID_PACKET;
    }

    /* Extract IP header */
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
    if (!ip_hdr)
    {
        return NAT_INVALID_PACKET;
    }

    /* Extract transport header */
    char *transport_hdr = (char *)(pkt_vaddr + transport_layer_offset(ip_hdr));

    /* Get pointers to port fields */
    uint16_t *src_port = (uint16_t *)(transport_hdr + nat->src_port_off);
    uint16_t *dst_port = (uint16_t *)(transport_hdr + nat->dst_port_off);
    uint16_t *check = (uint16_t *)(transport_hdr + nat->check_off);

    /* TODO */
    uint64_t now = 0;

    bool checksum_dirty = false;
    bool dnat_done = false;

    /* Log packet before translation */
    if (FW_DEBUG_OUTPUT)
    {
        sddf_printf("%s%s NAT Module: before translation:\n",
                    "iface",
                    "protocol");
        sddf_printf("%s%s NAT Module:   src = %s:%u\n",
                    "iface",
                    "protocol",
                    ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                    htons(*src_port));
        sddf_printf("%s%s NAT Module:   dst = %s:%u\n",
                    "iface",
                    "protocol",
                    ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0),
                    htons(*dst_port));
    }

    /* DNAT: returning traffic arrives on the external interface addressed to iface_ip:ephemeral_port;
     * reverse-map destination back to the original internal host. */
    if (do_dnat && nat->nat_enabled)
    {
        uint32_t iface_ip = nat->interface_config->ip;

        if (ip_hdr->dst_ip == iface_ip)
        {
            uint16_t dst_port_host = htons(*dst_port);

            if (dst_port_host >= nat->interface_config->base_port &&
                dst_port_host < nat->interface_config->base_port + nat->port_table->largest_index)
            {
                uint16_t table_index = dst_port_host - nat->interface_config->base_port;
                fw_nat_port_mapping_t *mapping = &nat->port_table->mappings[table_index];

                if (mapping->is_valid)
                {
                    if (FW_DEBUG_OUTPUT)
                    {
                        sddf_printf("%s%s NAT Module: returning traffic detected (DNAT)\n",
                                    "iface",
                                    "protocol");
                    }

                    mapping->last_used_ts = now;

                    *dst_port = mapping->src_port;
                    ip_hdr->dst_ip = mapping->src_ip;
                    ip_hdr->check = 0;

                    checksum_dirty = true;
                    nat->dnat_hits++;
                    dnat_done = true;
                }
            }
        }
    }

    /* SNAT: outbound traffic leaving the internal network; rewrite source to iface_ip:ephemeral_port
     * and record the mapping so returning traffic can be DNAT-ed back. */
    if (!dnat_done && nat->nat_enabled && ip_hdr->dst_ip != nat->interface_config->ip)
    {
        uint32_t iface_ip = nat->interface_config->ip;
        uint16_t ephemeral_port = fw_nat_find_ephemeral_port(
            *nat->interface_config,
            nat->port_table,
            ip_hdr->src_ip,
            *src_port,
            now);

        if (ephemeral_port)
        {
            ip_hdr->src_ip = iface_ip;
            *src_port = ephemeral_port;
            ip_hdr->check = 0;

            checksum_dirty = true;
            nat->snat_hits++;

            if (FW_DEBUG_OUTPUT)
            {
                sddf_printf("%s%s NAT Module: SNAT translated to %s:%u\n",
                            "iface",
                            "protocol",
                            ipaddr_to_string(iface_ip, ip_addr_buf0),
                            htons(*src_port));
            }
        }
        else
        {
            sddf_printf("%s%s NAT Module: ERROR: ephemeral ports exhausted!\n",
                        "iface",
                        "protocol");
            nat->translations_failed++;
            return NAT_PORT_EXHAUSTED;
        }
    }

    /* Recalculate checksum if headers were modified */
    if (checksum_dirty && nat->check_enabled)
    {
        *check = 0;
        *check = calculate_transport_checksum(
            transport_hdr,
            htons(ip_hdr->tot_len) - ipv4_header_length(ip_hdr),
            nat->protocol,
            ip_hdr->src_ip,
            ip_hdr->dst_ip);

        ip_hdr->check = 0;
        uint8_t ihl_bytes = ip_hdr->ihl * 4;
        ip_hdr->check = fw_internet_checksum(ip_hdr, ihl_bytes);
    }

    /* Log packet after translation */
    if (FW_DEBUG_OUTPUT)
    {
        sddf_printf("%s%s NAT Module: after translation:\n",
                    "iface",
                    "protocol");
        sddf_printf("%s%s NAT Module:   src = %s:%u\n",
                    "iface",
                    "protocol",
                    ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                    htons(*src_port));
        sddf_printf("%s%s NAT Module:   dst = %s:%u\n",
                    "iface",
                    "protocol",
                    ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0),
                    htons(*dst_port));
    }

    nat->translations_performed++;
    return NAT_SUCCESS;
}

/**
 * Cleanup expired NAT mappings
 */
int nat_module_cleanup_expired(nat_module_t *nat, uint64_t now)
{
    if (!nat || !nat->port_table || !nat->interface_config)
    {
        return NAT_FAILURE;
    }

    /* Use default timeout since it's no longer in webserver_state */
    uint64_t timeout = NAT_TIMEOUT_INTERVAL_NS;

    if (FW_DEBUG_OUTPUT)
    {
        uint16_t before = nat->port_table->size;
        fw_nat_free_expired_mappings(*nat->interface_config, nat->port_table, timeout, now);
        uint16_t after = nat->port_table->size;
        sddf_printf("%s%s NAT Module: cleanup completed, freed %u entries\n",
                    "iface",
                    "protocol",
                    before - after);
    }
    else
    {
        fw_nat_free_expired_mappings(*nat->interface_config, nat->port_table, timeout, now);
    }

    return NAT_SUCCESS;
}

/**
 * Get NAT module statistics
 */
void nat_module_get_stats(nat_module_t *nat,
                          uint64_t *translations_performed,
                          uint64_t *translations_failed,
                          uint64_t *dnat_hits,
                          uint64_t *snat_hits)
{
    if (!nat)
    {
        return;
    }

    if (translations_performed)
    {
        *translations_performed = nat->translations_performed;
    }
    if (translations_failed)
    {
        *translations_failed = nat->translations_failed;
    }
    if (dnat_hits)
    {
        *dnat_hits = nat->dnat_hits;
    }
    if (snat_hits)
    {
        *snat_hits = nat->snat_hits;
    }
}
