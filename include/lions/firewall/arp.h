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
#include <lions/firewall/ethernet.h>

/* ----------------- ARP Protocol Definitions ---------------------------*/

typedef struct __attribute__((__packed__)) arp_pkt {
    /* hardware type (network link layer protocol) */
    uint16_t hwtype;
    /* internetwork protocol for which the ARP request is intended */
    uint16_t protocol;
    /* length in bytes of a hardware address */
    uint8_t hwlen;
    /* length in bytes of internetwork addresses */
    uint8_t protolen;
    /* operation the sender is performing */
    uint16_t opcode;
    /* sender hardware address. In requests this is the address of the host
    sending the request. In a replies this is the address of the host the
    request was looking for */
    uint8_t hwsrc_addr[ETH_HWADDR_LEN];
    /* sender protocol address */
    uint32_t ipsrc_addr;
    /* target hardware address. In requests this is ignored. In replies this is
    the address of the host that originated the request */
    uint8_t hwdst_addr[ETH_HWADDR_LEN];
    /* target protocol address */
    uint32_t ipdst_addr;
    /* padding to reach the ethernet frame minimum payload */
    uint8_t padding[18];
} arp_pkt_t;

/* Offset of the start of the ARP packet */
#define ARP_PKT_OFFSET ETH_HDR_LEN

/* Length of ARP packet, including ethernet header */
#define ARP_PKT_LEN (ETH_HDR_LEN + sizeof(arp_pkt_t))

/* ARP hardware types*/
#define ARP_HWTYPE_ETH 1

/* ARP protocol address lengths */
#define ARP_PROTO_LEN_IPV4 4

/* ARP operation codes */
#define ARP_ETH_OPCODE_REQUEST 1
#define ARP_ETH_OPCODE_REPLY 2

/* ----------------- Firewall Data Types ---------------------------*/

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
