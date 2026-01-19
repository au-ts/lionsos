/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <sddf/network/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/array_functions.h>
#include <lions/firewall/config.h>

/**
 * Stores original source and destination corresponding to a NAT ephemeral port.
 * This is an endpoint independent mapping since only source address and port are used.
 */
typedef struct fw_nat_port_mapping {
    /* original source ip of traffic */
    uint32_t src_ip;
    /* original source port of traffic (network byte order) */
    uint16_t src_port;
} fw_nat_port_mapping_t;

typedef struct fw_nat_port_table {
    uint16_t size;
    fw_nat_port_mapping_t mappings[];
} fw_nat_port_table_t;

/**
 * Find the destination IP address and port for an incoming packet.
 * If the destination IP address matches the source NAT IP address
 * of the NAT on another interface, then the packet corresponds
 * to returning traffic and the port mapping corresponding
 * to that ephemeral port will be returned.
 *
 * @param interfaces configuration of NAT for each interface
 * @param dst_ip destination IP address on the packet
 * @param dst_port destination port in network byte order
 *
 * @return the original port mapping if it exists, NULL otherwise.
 */
static inline fw_nat_port_mapping_t *fw_nat_translate_destination(fw_nat_interface_config_t interfaces[],
                                                                  uint32_t dst_ip, uint16_t dst_port)
{
    /* Since dst_port is used as an index here it must be in host byte order */
    dst_port = htons(dst_port);

    for (uint16_t i = 0; i < FW_NUM_INTERFACES; i++) {
        if (dst_ip == interfaces[i].snat) {
            fw_nat_port_table_t *port_table = (fw_nat_port_table_t*)interfaces[i].port_table.vaddr;

            if ((dst_port >= interfaces[i].base_port)
                && (dst_port < interfaces[i].base_port + port_table->size)) {
                return &port_table->mappings[dst_port - interfaces[i].base_port];
            }
        }
    }

    return NULL;
}

/**
 * Find the ephemeral port to use for a source IP and port.
 * Attempts to reuse an existing mapping for that IP and port,
 * only creating a new entry if not found.
 *
 * @param config NAT config for this interface
 * @param ports ephemeral port table for this interface
 * @param src_ip source IP in network byte order
 * @param src_port source port in network byte order
 *
 * @returns ephemeral port in host byte order.
 */
static inline uint16_t fw_nat_find_ephemeral_port(fw_nat_interface_config_t config, fw_nat_port_table_t *ports,
                                                  uint32_t src_ip, uint16_t src_port)
{
    /* Search for an existing mapping */
    for (uint16_t i = 0; i < ports->size; i++) {
        if (ports->mappings[i].src_ip == src_ip && ports->mappings[i].src_port == src_port) {
            return config.base_port + i;
        }
    }

    if (ports->size >= config.ports_capacity) {
        return 0; /* Ephemeral ports pool is full */
    }

    /* Assign new ephemeral port */
    ports->mappings[ports->size].src_port = src_port;
    ports->mappings[ports->size].src_ip = src_ip;

    return config.base_port + ports->size++;
}
