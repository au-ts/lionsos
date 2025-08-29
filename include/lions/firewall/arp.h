/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/timer/client.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>

typedef enum {
    /* no error */
    ARP_ERR_OKAY = 0,
    /* data structure is full */
	ARP_ERR_FULL,
    /* arp entry invalid */
    ARP_ERR_INVALID
} fw_arp_error_t;

typedef enum {
    /* entry is not valid */
    ARP_STATE_INVALID = 0,
    /* IP is pending an arp response */
    ARP_STATE_PENDING,
    /* IP is unreachable */
    ARP_STATE_UNREACHABLE,
    /* IP is reachable, MAC address is valid */
    ARP_STATE_REACHABLE
} fw_arp_entry_state_t;

typedef struct fw_arp_entry {
    /* state of this entry */
    uint8_t state;
    /* IP address */
    uint32_t ip;
    /* MAC of IP if IP is reachable */
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* bitmap of clients that initiated the request */
    uint8_t client;
    /* number of arp requests sent for this IP address */
    uint8_t num_retries;
} fw_arp_entry_t;

typedef struct fw_arp_table {
    /* arp entries */
    fw_arp_entry_t *entries;
    /* capacity of arp table */
    uint16_t capacity;
} fw_arp_table_t;

typedef struct fw_arp_request {
    /* IP address */
    uint32_t ip;
    /* MAC address for IP if response and state is valid */
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* state of arp response */
    uint8_t state;
} fw_arp_request_t;

/**
 * Initialise the arp table data structure.
 *
 * @param table address of arp table.
 * @param entries virtual address of arp entries.
 * @param capacity capacity of arp table.
 */
static void fw_arp_table_init(fw_arp_table_t *table,
                              void *entries,
                              uint16_t capacity)
{
    table->entries = (fw_arp_entry_t *)entries;
    table->capacity = capacity;
}

/**
 * Find an arp entry for an ip address.
 *
 * @param table address of arp table.
 * @param ip ip address to lookup.
 *
 * @return address of arp entry of NULL.
 */
static fw_arp_entry_t *fw_arp_table_find_entry(fw_arp_table_t *table,
                                               uint32_t ip)
{
    for (uint16_t i = 0; i < table->capacity; i++) {
        fw_arp_entry_t *entry = table->entries + i;
        if (entry->state == ARP_STATE_INVALID) {
            continue;
        }

        if (entry->ip == ip) {
            return entry;
        }
    }

    return NULL;
}

/**
 * Create an arp response from an arp entry.
 *
 * @param entry address of arp entry to form response.
 *
 * @return arp response from entry.
 */
static fw_arp_request_t fw_arp_response_from_entry(fw_arp_entry_t *entry)
{
    fw_arp_request_t response = { 0 };
    if (entry == NULL) {
        return response;
    }

    response.ip = entry->ip;
    response.state = entry->state;
    if (entry->state == ARP_STATE_REACHABLE) {
        memcpy(&response.mac_addr, &entry->mac_addr, ETH_HWADDR_LEN);
    }

    return response;
}

/**
 * Add an entry to the arp table.
 *
 * @param table address of arp table.
 * @param timer_ch channel to sddf timer subsystem.
 * @param state state of arp entry.
 * @param ip ip address of arp entry.
 * @param mac_addr mac address of arp entry or NULL.
 * @param client client that initiated arp request.
 *
 * @return error status.
 */
static fw_arp_error_t fw_arp_table_add_entry(fw_arp_table_t *table,
                                       fw_arp_entry_state_t state,
                                       uint32_t ip,
                                       uint8_t *mac_addr,
                                       uint8_t client)
{
    if (state == ARP_STATE_REACHABLE && mac_addr == NULL) {
        return ARP_ERR_INVALID;
    }

    fw_arp_entry_t *slot = NULL;
    for (uint16_t i = 0; i < table->capacity; i++) {
        fw_arp_entry_t *entry = table->entries + i;

        if (entry->state == ARP_STATE_INVALID) {
            if (slot == NULL) {
                slot = entry;
            }
            continue;
        }

        /* Check for existing entries for this ip - there should only be one */
        if (entry->ip == ip) {
            slot = entry;
            break;
        }
    }

    if (slot == NULL) {
        return ARP_ERR_FULL;
    }

    slot->state = state;
    slot->ip = ip;
    if (state == ARP_STATE_REACHABLE) {
        memcpy(&slot->mac_addr, mac_addr, ETH_HWADDR_LEN);
    }
    slot->client = BIT(client);
    slot->num_retries = 0;

    return ARP_ERR_OKAY;
}
