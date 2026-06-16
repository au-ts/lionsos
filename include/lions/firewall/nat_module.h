/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/timer/protocol.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>

typedef enum {
    /* no error */
    NAT_ERR_OKAY = 0,
    /* null pointer passed or port table not initialised */
    NAT_ERR_FAILURE,
    /* all ephemeral ports in the table are in use */
    NAT_ERR_PORT_EXHAUSTED,
    /* packet is too short or has an invalid IP header */
    NAT_ERR_INVALID_PACKET,
} fw_nat_err_t;

/* NAT timeout interval in nanoseconds */
#define NAT_TIMEOUT_INTERVAL_NS (5 * NS_IN_S)

/**
 * NAT table structures
 */

/**
 * Stores original source and destination corresponding to a NAT ephemeral port.
 * This is an endpoint independent mapping since only source address and port are used.
 */
typedef struct fw_nat_port_mapping fw_nat_port_mapping_t;
struct fw_nat_port_mapping
{
    /* Original source IP of traffic */
    uint32_t src_ip;
    /* Original source port of traffic (network byte order) */
    uint16_t src_port;
    /* Next free node (for nodes in free list only) */
    fw_nat_port_mapping_t *next_free;
    bool is_valid;
    /* SDDF timer timestamp (nanoseconds) for last time a packet was sent/received */
    uint64_t last_used_ts;
};

/**
 * Port table that manages ephemeral port allocations
 */
typedef struct fw_nat_port_table
{
    /* Runtime enable flag*/
    bool nat_enabled;
    /* Number of valid NAT entries */
    uint16_t size;
    /* Largest initialized entry in the NAT table (could be valid or free) */
    uint16_t largest_index;
    /* Head of free nodes */
    fw_nat_port_mapping_t *free_head;
    fw_nat_port_mapping_t mappings[];
} fw_nat_port_table_t;

/* Magic number to indicate webserver_state has been initialized */
#define FW_NAT_WEBSERVER_STATE_MAGIC 0x4E415457

/**
 * Structure shared with webserver to configure NAT for all interfaces with this protocol.
 * The SNAT IP is static (from build-time config); only enable/disable and timeout are runtime.
 */
typedef struct fw_nat_webserver_state
{
    uint32_t magic; /* Magic number for initialization check */
    /* Timeout in nanoseconds */
    uint64_t timeout;
} fw_nat_webserver_state_t;


/**
 * Find the ephemeral port to use for a source IP and port.
 * Attempts to reuse an existing mapping for that IP and port,
 * only creating a new entry if not found.
 *
 * @param config NAT config for this interface
 * @param ports Ephemeral port table for this interface
 * @param src_ip Source IP in network byte order
 * @param src_port Source port in network byte order
 * @param now Current timestamp in nanoseconds
 *
 * @return Ephemeral port in network byte order, or 0 if no port available
 */
static inline uint16_t fw_nat_find_ephemeral_port(fw_nat_port_table_config_t config,
                                                  fw_nat_port_table_t *ports,
                                                  uint32_t src_ip,
                                                  uint16_t src_port,
                                                  uint64_t now)
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
        mapping->is_valid = true;
        return htons(config.base_port + (mapping - ports->mappings));
    }

    if (ports->size >= config.ports_capacity)
    {
        return 0; /* Ephemeral ports pool is full */
    }

    /* Initialize a new entry in the NAT table and assign its ephemeral port */
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
 * @param ports Ephemeral port table for this interface
 * @param port Ephemeral port to be freed in host byte order
 */
static inline void fw_nat_free_ephemeral_port(fw_nat_port_table_config_t config,
                                              fw_nat_port_table_t *ports,
                                              uint16_t port)
{
    fw_nat_port_mapping_t *mapping = &ports->mappings[port - config.base_port];

    mapping->src_ip = 0;
    mapping->src_port = 0;
    mapping->next_free = ports->free_head;
    mapping->is_valid = false;
    mapping->last_used_ts = 0;

    /* This index is now the new head */
    ports->free_head = mapping;

    ports->size--;
}

/**
 * Frees all port mappings older than the timeout duration.
 *
 * @param config NAT config for this interface
 * @param ports Ephemeral port table for this interface
 * @param timeout Duration in nanoseconds for which entries older than it will be freed
 * @param now The time now as an SDDF timestamp
 */
static inline void fw_nat_free_expired_mappings(fw_nat_port_table_config_t config,
                                                fw_nat_port_table_t *ports,
                                                uint64_t timeout,
                                                uint64_t now)
{
    for (uint16_t i = 0; i < ports->largest_index; i++)
    {
        fw_nat_port_mapping_t *mapping = &ports->mappings[i];

        if (mapping->is_valid && now > timeout && mapping->last_used_ts <= now - timeout)
        {
            fw_nat_free_ephemeral_port(config, ports, config.base_port + i);

#ifdef FW_DEBUG_OUTPUT
            sddf_printf("NAT LOG: freed port: %u, %u remaining\n", config.base_port + i, ports->size);
#endif
        }
    }
}

/**
 * NAT module handle
 *
 * This structure encapsulates all state needed for NAT translation
 * within a single component. It references shared memory structures
 * for NAT tables and configuration.
 */
typedef struct nat_module
{
    /* Interface identifier */
    uint8_t interface;

    /* Protocol (IPPROTO_TCP or IPPROTO_UDP) */
    uint8_t protocol;

    /* Port table reference (shared memory) — contains nat_enabled flag */
    fw_nat_port_table_t *port_table;

    /* Port table configuration (base port, capacity) */
    fw_nat_port_table_config_t *config;

    /* IP address of the firewall's outbound interface */
    uint32_t interface_ip;

    /* Byte offsets for protocol-specific header parsing */
    size_t src_port_off; /* Offset to source port in transport header */
    size_t dst_port_off; /* Offset to destination port in transport header */
    size_t check_off;    /* Offset to checksum in transport header */

    /* Whether to recalculate checksum */
    bool check_enabled;

} nat_module_t;

/**
 * Initialize the NAT module
 *
 * @param nat Pointer to NAT module structure to initialize
 * @param interface Interface identifier (0 or 1)
 * @param protocol Protocol (IPPROTO_TCP or IPPROTO_UDP)
 * @param interface_config Pointer to interface configuration (shared memory)
 * @param port_table Pointer to port table (shared memory)
 * @param src_port_off Byte offset to source port in transport header
 * @param dst_port_off Byte offset to destination port in transport header
 * @param check_off Byte offset to checksum in transport header
 * @param check_enabled Whether to recalculate checksums
 *
 * @return NAT_ERR_OKAY on success, NAT_ERR_FAILURE on error
 */
fw_nat_err_t nat_module_init(nat_module_t *nat,
                    uint8_t interface,
                    uint8_t protocol,
                    fw_nat_port_table_config_t *config,
                    fw_nat_port_table_t *port_table,
                    uint32_t interface_ip,
                    size_t src_port_off,
                    size_t dst_port_off,
                    size_t check_off,
                    bool check_enabled);

/**
 * Translate a packet using NAT
 *
 * This function performs both DNAT (for returning traffic) and SNAT (for outbound traffic).
 * It modifies the packet headers in-place and updates checksums if enabled.
 *
 * @param nat Pointer to initialized NAT module
 * @param pkt_vaddr Virtual address of packet data
 * @param buffer Buffer descriptor (for potential future use)
 * @param do_dnat True on the inbound (external→internal) path: attempt DNAT before SNAT.
 *                False on the outbound (internal→external) path: only SNAT applies.
 *
 * @return NAT_ERR_OKAY on success, NAT_ERR_PORT_EXHAUSTED if no ephemeral ports available,
 *         NAT_ERR_INVALID_PACKET if packet is malformed.
 */
fw_nat_err_t nat_module_translate(nat_module_t *nat,
                         uintptr_t pkt_vaddr,
                         net_buff_desc_t *buffer,
                         bool do_dnat);

