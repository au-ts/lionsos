/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include "sddf/util/printf.h"
#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <sddf/network/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/array_functions.h>
#include <lions/firewall/config.h>

#define NAT_TIMEOUT_INTERVAL_NS (5 * NS_IN_S)
#define FW_MAX_PORT_FORWARDING_RULES 64

/**
 * Port forwarding rule structure
 * Maps an external port on a NAT interface to an internal IP and port
 */
typedef struct fw_nat_port_forwarding_rule
{
    /* Whether this rule is active */
    bool is_valid;
    uint8_t protocol;
    /* External port on NAT interface */
    uint16_t external_port;
    /* Internal destination IP address */
    uint32_t internal_ip;
    /* Internal destination port */
    uint16_t internal_port;
    /* Interface this rule applies to */
    uint8_t interface;
} fw_nat_port_forwarding_rule_t;

/**
 * Table of port forwarding rules
 */
typedef struct fw_nat_port_forwarding_table
{
    fw_nat_port_forwarding_rule_t rules[FW_MAX_PORT_FORWARDING_RULES];
    uint16_t num_rules;
} fw_nat_port_forwarding_table_t;

/* Holds webserver state specific to the NAT of a particular interface */
typedef struct fw_nat_webserver_interface_state
{
    /* Source NAT IP */
    uint32_t snat;
} fw_nat_webserver_interface_state_t;

/**
 * Structure shared with webserver to configure NAT for all interfaces with this protocol
 */
typedef struct fw_nat_webserver_state
{
    fw_nat_webserver_interface_state_t interfaces[FW_NUM_INTERFACES];
    uint64_t timeout;
    /* Port forwarding rules table */
    fw_nat_port_forwarding_table_t port_forwarding;
} fw_nat_webserver_state_t;

/**
 * Stores original source and destination corresponding to a NAT ephemeral port.
 * This is an endpoint independent mapping since only source address and port are used.
 */
typedef struct fw_nat_port_mapping fw_nat_port_mapping_t;
struct fw_nat_port_mapping
{
    /* original source ip of traffic */
    uint32_t src_ip;
    /* original source port of traffic (network byte order) */
    uint16_t src_port;
    /* next free node (for nodes in free list only) */
    fw_nat_port_mapping_t *next_free;
    bool is_valid;
    /* sddf timer timestamp (nanoseconds) for last time a packet was sent or received on this port */
    uint64_t last_used_ts;
};

typedef struct fw_nat_port_table
{
    /* number of valid NAT entries */
    uint16_t size;
    /* largest initialised entry in the NAT table (could be valid or free) */
    uint16_t largest_index;
    /* head of free nodes */
    fw_nat_port_mapping_t *free_head;
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
static inline fw_nat_port_mapping_t *fw_nat_translate_destination(fw_nat_interface_config_t interfaces[], fw_nat_webserver_state_t state,
                                                                  uint32_t dst_ip, uint16_t dst_port, uint64_t now)
{
    /* Since dst_port is used as an index here it must be in host byte order */
    dst_port = htons(dst_port);

    for (uint16_t i = 0; i < FW_NUM_INTERFACES; i++)
    {
        if (dst_ip == state.interfaces[i].snat)
        {
            fw_nat_port_table_t *port_table = (fw_nat_port_table_t *)interfaces[i].port_table.vaddr;

            if ((dst_port >= interfaces[i].base_port) && (dst_port < interfaces[i].base_port + port_table->largest_index) && port_table->mappings[dst_port - interfaces[i].base_port].is_valid)
            {
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
 * @returns ephemeral port in network byte order.
 */
static inline uint16_t fw_nat_find_ephemeral_port(fw_nat_interface_config_t config, fw_nat_port_table_t *ports,
                                                  uint32_t src_ip, uint16_t src_port, uint64_t now)
{
    /* Search for an existing mapping */
    for (uint16_t i = 0; i < ports->size; i++)
    {
        if (ports->mappings[i].src_ip == src_ip && ports->mappings[i].src_port == src_port && ports->mappings[i].is_valid)
        {
            ports->mappings[i].last_used_ts = now;
            return htons(config.base_port + i);
        }
    }

    /* Try to reuse a free entry */
    if (ports->free_head)
    {
        ports->size++;

        /* Remove from front of list */
        fw_nat_port_mapping_t *mapping = ports->free_head;
        ports->free_head = mapping->next_free;

        mapping->src_port = src_port;
        mapping->src_ip = src_ip;
        mapping->last_used_ts = now;
        ports->mappings[ports->largest_index].is_valid = true;
        return htons(config.base_port + mapping - ports->mappings);
    }

    if (ports->size >= config.ports_capacity)
    {
        return 0; /* Ephemeral ports pool is full */
    }

    /* Initialise a new entry in the NAT table and assign its ephemeral port */
    ports->size++;
    ports->mappings[ports->largest_index].src_port = src_port;
    ports->mappings[ports->largest_index].src_ip = src_ip;
    ports->mappings[ports->largest_index].is_valid = true;
    ports->mappings[ports->largest_index].last_used_ts = now;

    return htons(config.base_port + ports->largest_index++);
}

/**
 * Free an ephemeral port, prepending it to the head of the free list.
 *
 * @param config NAT config for this interface
 * @param ports ephemeral port table for this interface
 * @param port ephemeral port to be freed in host byte order
 */
static inline void fw_nat_free_ephemeral_port(fw_nat_interface_config_t config, fw_nat_port_table_t *ports,
                                              uint16_t port)
{
    fw_nat_port_mapping_t *mapping = &ports->mappings[port - config.base_port];

    sddf_printf("NAT LOG: free head is now: %p\n", ports->free_head);
    mapping->src_ip = 0;
    mapping->src_port = 0;
    mapping->next_free = ports->free_head;
    mapping->is_valid = false;
    mapping->last_used_ts = 0;

    /* this index is now the new head */
    ports->free_head = mapping;
    sddf_printf("NAT LOG: free head is now: %p\n", ports->free_head);

    ports->size--;
}

/**
 * Frees all port mappings older than the timeout duration.
 * @param config NAT config for this interface
 * @param ports ephemeral port table for this interface
 * @param timeout duration in nanoseconds for which entries older than it will be freed
 * @param now the time now as an SDDF timestamp
 */
static inline void fw_nat_free_expired_mappings(fw_nat_interface_config_t config, fw_nat_port_table_t *ports,
                                                uint64_t timeout, uint64_t now)
{
    for (uint16_t i = 0; i < ports->largest_index; i++)
    {
        fw_nat_port_mapping_t *mapping = &ports->mappings[i];

        if (mapping->is_valid && now > timeout && mapping->last_used_ts <= now - timeout)
        {
            fw_nat_free_ephemeral_port(config, ports, config.base_port + i);

            if (FW_DEBUG_OUTPUT)
            {
                sddf_printf("NAT LOG: freed port: %u, %u remaining\n", config.base_port + i, ports->size);
            }
        }
    }
}

/**
 * Add a port forwarding rule to the table.
 *
 * @param table Port forwarding table
 * @param protocol IPPROTO_TCP or IPPROTO_UDP
 * @param external_port External port
 * @param internal_ip Internal destination IP
 * @param internal_port Internal destination port
 * @param interface Interface index
 * @return 0 on success, -1 on error
 */
static inline int fw_nat_add_port_forwarding_rule(fw_nat_port_forwarding_table_t *table,
                                                  uint8_t protocol, uint16_t external_port,
                                                  uint32_t internal_ip, uint16_t internal_port,
                                                  uint8_t interface)
{
    if (table->num_rules >= FW_MAX_PORT_FORWARDING_RULES)
    {
        return -1; /* Table is full */
    }

    /* Check for duplicate rules */
    for (uint16_t i = 0; i < table->num_rules; i++)
    {
        if (table->rules[i].is_valid && table->rules[i].protocol == protocol &&
            table->rules[i].external_port == external_port && table->rules[i].interface == interface)
        {
            return -1; /* Duplicate rule */
        }
    }

    /* Find an empty slot */
    uint16_t slot = table->num_rules;
    table->rules[slot].is_valid = true;
    table->rules[slot].protocol = protocol;
    table->rules[slot].external_port = external_port;
    table->rules[slot].internal_ip = internal_ip;
    table->rules[slot].internal_port = internal_port;
    table->rules[slot].interface = interface;
    table->num_rules++;

    return 0;
}

/**
 * Remove a port forwarding rule from the table.
 *
 * @param table Port forwarding table
 * @param index Index of rule to remove
 * @return 0 on success, -1 on error
 */
static inline int fw_nat_remove_port_forwarding_rule(fw_nat_port_forwarding_table_t *table, uint16_t index)
{
    if (index >= FW_MAX_PORT_FORWARDING_RULES || !table->rules[index].is_valid)
    {
        return -1;
    }

    /* Mark rule as invalid */
    table->rules[index].is_valid = false;

    /* Shift remaining rules to fill the gap */
    for (uint16_t i = index; i < table->num_rules - 1; i++)
    {
        table->rules[i] = table->rules[i + 1];
    }

    table->num_rules--;
    return 0;
}

/**
 * Find a port forwarding rule matching the given criteria.
 *
 * @param table Port forwarding table
 * @param protocol IPPROTO_TCP or IPPROTO_UDP
 * @param external_port External port to match
 * @param interface Interface index
 * @return Pointer to matching rule, or NULL if not found
 */
static inline fw_nat_port_forwarding_rule_t *fw_nat_find_port_forwarding_rule(
    fw_nat_port_forwarding_table_t *table, uint8_t protocol, uint16_t external_port, uint8_t interface)
{
    for (uint16_t i = 0; i < table->num_rules; i++)
    {
        if (table->rules[i].is_valid && table->rules[i].protocol == protocol &&
            table->rules[i].external_port == external_port && table->rules[i].interface == interface)
        {
            return &table->rules[i];
        }
    }

    return NULL;
}
