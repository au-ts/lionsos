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

#include <stdlib.h>

#define MAX_CUCKOO_RETRIES 10
#define RANDOM_PRIME 11
#define MAP_BUCKET_SIZE 4

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
    /* capacity of arp table */
    uint16_t capacity;
    /* arp entries */
    fw_arp_entry_t entries[]; 
} fw_arp_table_t;

typedef struct fw_arp_request {
    /* IP address */
    uint32_t ip;
    /* MAC address for IP if response and state is valid */
    uint8_t mac_addr[ETH_HWADDR_LEN];
    /* state of arp response */
    uint8_t state;
} fw_arp_request_t;

static uint32_t hash1(fw_arp_table_t* table, uint32_t ip) {
    return ip*RANDOM_PRIME*3 % (table->capacity/4);
}

static uint32_t hash2(fw_arp_table_t* table, uint32_t ip) {
    return ip*RANDOM_PRIME % (table->capacity/4);
}

/*
This loops over a bucket in the map and inserts in a free slot
*/
static bool map_bucket_insert(fw_arp_table_t* table,
                        uint32_t bucket,
                        uint32_t ip,
                        uint8_t *mac_addr,
                        uint8_t client,
                        fw_arp_entry_state_t state) {
    for (int i = 0; i < MAP_BUCKET_SIZE; i++) {
        fw_arp_entry_t* entry = &table->entries[bucket + i];
        if (entry->state == ARP_STATE_INVALID) {
            entry->ip = ip;
            if (mac_addr != NULL) {
                memcpy(&entry->mac_addr, mac_addr, ETH_HWADDR_LEN);
            }
            entry->state = state;
            entry->num_retries = 0;
            entry->client = BIT(client);
            return true;
        }
    }
    return false;                    
}

// make space in b1 by moving the value into b2
// if fails randomly evict something from bucket 2
static void map_cuckoo_step(fw_arp_table_t* table, uint32_t move_item_idx, uint8_t cuckoo_retries) {
    if (cuckoo_retries == MAX_CUCKOO_RETRIES) {
        table->entries[move_item_idx].state = ARP_STATE_INVALID;
        // when you reach the limit you loose the data of that last item
        return;
    }
    
    fw_arp_entry_t entry_to_move = table->entries[move_item_idx];

    uint32_t b2 = hash2(table, entry_to_move.ip) * MAP_BUCKET_SIZE;
    if (!map_bucket_insert(table, b2, entry_to_move.ip, entry_to_move.mac_addr, entry_to_move.client,entry_to_move.state )) {
        // this is because the 2nd bucket is full hence we remove from this bucket randomly and repeat
        uint32_t bucket2_index = b2 + (rand() & 0b11);
        map_cuckoo_step(table, bucket2_index, cuckoo_retries + 1); 
        
        // insert into the free slot in b2 
        map_bucket_insert(table, b2, entry_to_move.ip, entry_to_move.mac_addr, entry_to_move.client,entry_to_move.state );
    } 
    table->entries[move_item_idx].state = ARP_STATE_INVALID;
}

static void map_insert(fw_arp_table_t* table, 
                        uint32_t ip,
                        uint8_t *mac_addr,
                        uint8_t client,
                        uint8_t cuckoo_retries, 
                        fw_arp_entry_state_t state) {
   
    uint32_t b1 = hash1(table, ip)*MAP_BUCKET_SIZE;
    if (!map_bucket_insert(table, b1, ip, mac_addr, client, state)) {
        uint32_t bucket1_index = b1 + (rand() & 0b11);
        map_cuckoo_step(table, bucket1_index, 0);
    }
    map_bucket_insert(table, b1, ip, mac_addr, client, state);
}
static fw_arp_entry_t* map_bucket_search(fw_arp_table_t* table, uint32_t ip, uint32_t bucket) {
    for (int i = 0; i < MAP_BUCKET_SIZE; i++) {
        fw_arp_entry_t* entry = &table->entries[bucket + i];
        if (entry->state != ARP_STATE_INVALID && entry->ip == ip) {
            return entry;
        }
    }
    return NULL;
} 

static bool map_search(fw_arp_table_t* table, uint32_t ip, fw_arp_entry_t** res) {
    uint32_t b1 = hash1(table, ip)*MAP_BUCKET_SIZE;
    // search b1
    *res = map_bucket_search(table, ip, b1);
    if (*res != NULL) {
        return true;
    } 
    uint32_t b2 = hash2(table, ip)*MAP_BUCKET_SIZE;
    // search b2
    *res = map_bucket_search(table, ip, b2);
    if (*res != NULL) {
        return true;
    }

    return false;
}

/**
 * Initialise the arp table data structure.
 *
 * @param table address of arp table.
 * @param entries virtual address of arp entries.
 * @param capacity capacity of arp table.
 */
static void fw_arp_table_init(fw_arp_table_t **table,
                              void *arp_table_vaddr,
                              uint16_t capacity)
{
    *table = (fw_arp_table_t*)arp_table_vaddr;
    (*table)->capacity = capacity;
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
    fw_arp_entry_t* entry;
    map_search(table, ip, &entry);
    return entry;
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
    fw_arp_entry_t *slot;
    if (!map_search(table, ip, &slot)) {
        map_insert(table, ip, mac_addr, client, 0, state);
    }
    return ARP_ERR_OKAY;
}
