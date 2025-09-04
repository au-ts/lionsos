/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sddf/network/queue.h>
#include <lions/firewall/array_functions.h>
#include <lions/firewall/common.h>
#include <lions/firewall/queue.h>

/* IP of no next hop */
#define FW_ROUTING_NONEXTHOP 0

/* maximum number of recursive calls that fw_routing_find_route makes to resolve
a route for an ip address */
#define FW_ROUTING_MAX_RECURSION 3

typedef enum {
    /* no error */
    ROUTING_ERR_OKAY = 0,
    /* data structure is full */
	ROUTING_ERR_FULL,
    /* duplicate entry exists */
	ROUTING_ERR_DUPLICATE,
    /* entry clashes with existing entry */
    ROUTING_ERR_CLASH,
    /* node ID does not point to a valid node */
    ROUTING_ERR_INVALID_ID,
    /* route is invalid */
    ROUTING_ERR_INVALID_ROUTE
} fw_routing_err_t;

static const char *fw_routing_err_str[] = {
    "Ok.",
    "Out of memory error.",
    "Duplicate entry.",
    "Clashing entry.",
    "Invalid child node.",
    "Invalid route ID.",
    "Invalid route values."
};

/* routing interfaces */
typedef enum {
    /* no interface */
    ROUTING_OUT_NONE = 0,
    /* transmit out NIC */
    ROUTING_OUT_EXTERNAL,
    /* transmit internally within the system */
	ROUTING_OUT_SELF
} fw_routing_interfaces_t;

/* PP call parameters for webserver to call router and update routing table */
#define FW_ADD_ROUTE 0
#define FW_DEL_ROUTE 1

typedef enum {
    ROUTER_ARG_ROUTE_ID = 0,
    ROUTER_ARG_IP,
    ROUTER_ARG_SUBNET,
    ROUTER_ARG_NEXT_HOP
} fw_router_args_t;

typedef enum {
    ROUTER_RET_ERR = 0
} fw_router_ret_args_t;

typedef struct routing_entry {
    /* ip address of destination subnet */
    uint32_t ip;
    /* number of bits in subnet mask */
    uint8_t subnet;
    /* interface subnet traffic should be transmitted through */
    uint8_t interface;
    /* ip address of next hop */
    uint32_t next_hop;
} fw_routing_entry_t;

typedef struct routing_table {
    /* capacity of table */
    uint16_t capacity;
    /* number of valid entries in table */
    uint16_t size;
    /* routing table entries stored consecutively */
    fw_routing_entry_t entries[];
} fw_routing_table_t;

/* packet waiting node used to store outgoing packets before MAC address has
been resolved */
typedef struct pkt_waiting_node {
    /* next packet waiting node */
    uint16_t next;
    /* previous packet waiting node, only maintained for nodes in use */
    uint16_t prev;
    /* child packet waiting node, nodes destined for same ip */
    uint16_t child;
    /* number of child nodes, only maintained for root node */
    uint16_t num_children;
    /* destination ip for this packet and child packets, only maintained for
    root node */
    uint32_t ip;
    /* buffer of outgoing packet */
    net_buff_desc_t buffer;
} pkt_waiting_node_t;

typedef struct pkts_waiting {
    /* address of packet node table */
    pkt_waiting_node_t *packets;
    /* capacity of packet node table */
    uint16_t capacity;
    /* number of nodes in use */
    uint16_t size;
    /* number of root nodes */
    uint16_t length;
    /* head of root nodes */
    uint16_t head;
    /* tail of root nodes */
    uint16_t tail;
    /* head of free nodes */
    uint16_t free;
} pkts_waiting_t;

/**
 * Initialise packet waiting structure.
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param packets virtual address of packets.
 * @param capacity number of available packet waiting nodes.
 */
static void pkt_waiting_init(pkts_waiting_t *pkts_waiting,
                      void *packets,
                      int16_t capacity)
{
    pkts_waiting->packets = (pkt_waiting_node_t *)packets;
    pkts_waiting->capacity = capacity;
    for (uint16_t i = 0; i < pkts_waiting->capacity; i++) {
        pkt_waiting_node_t *node = pkts_waiting->packets + i;
        /* Free list only maintains next pointers */
        node->next = i + 1;
    }
}

/**
 * Check if the packet waiting queue is full.
 *
 * @param pkts_waiting address of packets waiting structure.
 *
 * @return whether packet waiting queue is full.
 */
static bool pkt_waiting_full(pkts_waiting_t *pkts_waiting)
{
    return pkts_waiting->size == pkts_waiting->capacity;
}

/**
 * Find matching ip packet waiting node in packet waiting list.
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param ip ip adress to match with.
 *
 * @return address of matching packet waiting root node or NULL if no match.
 */
static pkt_waiting_node_t *pkt_waiting_find_node(pkts_waiting_t *pkts_waiting,
                                          uint32_t ip)
{
    pkt_waiting_node_t *node = pkts_waiting->packets + pkts_waiting->head;
    for (uint16_t i = 0; i < pkts_waiting->length; i++) {
        if (node->ip == ip) {
            return node;
        }
        node = pkts_waiting->packets + node->next;
    }

    return NULL;
}

/**
 * Return the next child node, assumes child node is valid!
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param node parent node.
 *
 * @return address of child node.
 */
static pkt_waiting_node_t *pkts_waiting_next_child(pkts_waiting_t *pkts_waiting,
                                            pkt_waiting_node_t *node)
{
    return pkts_waiting->packets + node->child;
}

/**
 * Add a child node to a root waiting node. Node passed must be a root node!
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param root root node.
 * @param buffer buffer holding outgoing packet to be stored in new node.
 *
 * @return error status of operation.
 */
static fw_routing_err_t pkt_waiting_push_child(pkts_waiting_t *pkts_waiting,
                                        pkt_waiting_node_t *root,
                                        net_buff_desc_t buffer)
{
    if (pkt_waiting_full(pkts_waiting)) {
        return ROUTING_ERR_FULL;
    }

    uint16_t new_idx = pkts_waiting->free;
    pkt_waiting_node_t *new_node = pkts_waiting->packets + new_idx;

    /* Update values */
    new_node->buffer = buffer;

    /* Update pointers */
    pkts_waiting->free = new_node->next;
    pkt_waiting_node_t *last_child = root;
    for (uint16_t i = 0; i < root->num_children; i++) {
        last_child = pkts_waiting_next_child(pkts_waiting, last_child);
    }
    last_child->child = new_idx;

    /* Update counts */
    root->num_children++;
    pkts_waiting->size++;

    return ROUTING_ERR_OKAY;
}

/**
 * Add a new root node to IP packet list. Assumes no valid root node for IP.
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param ip IP address of outgoing packet stored in new node.
 * @param buffer buffer holding outgoing packet to be stored in new node.
 *
 * @return error status of operation.
 */
static fw_routing_err_t pkt_waiting_push(pkts_waiting_t *pkts_waiting,
                                  uint32_t ip,
                                  net_buff_desc_t buffer)
{
    if (pkt_waiting_full(pkts_waiting)) {
        return ROUTING_ERR_FULL;
    }

    uint16_t new_idx = pkts_waiting->free;
    pkt_waiting_node_t *new_node = pkts_waiting->packets + new_idx;

    /* Update values */
    new_node->num_children = 0;
    new_node->ip = ip;
    new_node->buffer = buffer;

    /* Update pointers */
    pkts_waiting->free = new_node->next;
    /* If this is not the first node */
    if (pkts_waiting->length) {
        uint16_t head_idx = pkts_waiting->head;
        pkt_waiting_node_t *head_node = pkts_waiting->packets + head_idx;

        new_node->next = head_idx;
        head_node->prev = new_idx;
    } else {
        pkts_waiting->tail = new_idx;
    }
    pkts_waiting->head = new_idx;

    /* Update counts */
    pkts_waiting->length++;
    pkts_waiting->size++;

    return ROUTING_ERR_OKAY;
}

/**
 * Free a node and all its children. Must pass a root node!
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param root root node to free.
 *
 * @return error status of operation.
 */
static fw_routing_err_t pkts_waiting_free_parent(pkts_waiting_t *pkts_waiting,
                                          pkt_waiting_node_t *root)
{
    /* First free children */
    uint16_t child_idx = root->child;
    pkt_waiting_node_t *child_node = pkts_waiting_next_child(pkts_waiting, root);
    for (uint16_t i = 0; i < root->num_children; i++) {

        /* Add to free list */
        child_node->next = pkts_waiting->free;
        pkts_waiting->free = child_idx;
        pkts_waiting->size--;

        /* Possibly free next child */
        child_idx = child_node->child;
        child_node = pkts_waiting_next_child(pkts_waiting, child_node);
    }

    /* Now free parent */
    uint16_t root_idx = (uint16_t)(root - pkts_waiting->packets);
    if (root_idx == pkts_waiting->head) {
        /* Root node is head */
        pkts_waiting->head = root->next;
    } else {
        pkt_waiting_node_t *prev_node = pkts_waiting->packets + root->prev;
        prev_node->next = root->next;
    }

    if (root_idx == pkts_waiting->tail) {
        /* Root node is tail */
        pkts_waiting->tail = root->prev;
    } else {
        pkt_waiting_node_t *next_node = pkts_waiting->packets + root->next;
        next_node->prev = root->prev;
    }

    root->next = pkts_waiting->free;
    pkts_waiting->free = root_idx;
    pkts_waiting->length--;
    pkts_waiting->size--;

    return ROUTING_ERR_OKAY;
}

/**
 * Find next hop for destination IP. Maximum recursion limit to prevent infinite
 * looping.
 *
 * @param table address of routing table.
 * @param ip IP address to find route to.
 * @param next_hop address to store IP of next hop.
 * @param interface interface traffic should be routed out.
 * @param num_calls number of times fw_routing_find_route has been called
 * recursively. Pass 0 for maximum number of recursive calls.
 *
 * @return error status of operation.
 */
static fw_routing_err_t fw_routing_find_route(fw_routing_table_t *table,
                                      uint32_t ip,
                                      uint32_t *next_hop,
                                      fw_routing_interfaces_t *interface,
                                      uint8_t num_calls)
{
    fw_routing_entry_t *match = NULL;
    for (uint16_t i = 0; i < table->size; i++) {
        fw_routing_entry_t *entry = table->entries + i;

        /* ip is part of subnet */
        if ((subnet_mask(entry->subnet) & ip) == entry->ip) {

            /* Current match is stronger */
            if (match != NULL && match->subnet > entry->subnet) {
                continue;
            }

            match = entry;
        }
    }

    if (match == NULL) {
        /* No route found */
        *interface = ROUTING_OUT_NONE;
    } else if (match->interface == ROUTING_OUT_SELF) {
        /* Route internally */
        *interface = ROUTING_OUT_SELF;
    } else if (match->interface == ROUTING_OUT_EXTERNAL &&
               match->next_hop == FW_ROUTING_NONEXTHOP) {
        *next_hop = ip;
        *interface = ROUTING_OUT_EXTERNAL;
    } else if (match->interface == ROUTING_OUT_EXTERNAL &&
               match->next_hop != FW_ROUTING_NONEXTHOP) {
        num_calls ++;
        if (num_calls == FW_ROUTING_MAX_RECURSION) {
            /* Find route has hit recursive call limit, ip unreachable. */
            *interface = ROUTING_OUT_NONE;
            return ROUTING_ERR_OKAY;
        }
        fw_routing_err_t err = fw_routing_find_route(table,
                                                 match->next_hop,
                                                    next_hop,
                                                    interface,
                                                    num_calls);
        return err;
    }

    return ROUTING_ERR_OKAY;
}

/**
 * Add a route to the routing table.
 *
 * @param table address of routing table.
 * @param interface interface route should be routed out.
 * @param ip IP address of route.
 * @param subnet subnet bits of route.
 * @param next_hop next hop IP adress of route.
 *
 * @return error status of operation.
 */
static fw_routing_err_t fw_routing_table_add_route(fw_routing_table_t *table,
                                                   fw_routing_interfaces_t interface,
                                                   uint32_t ip,
                                                   uint8_t subnet,
                                                   uint32_t next_hop)
{
    /* Default routes must specify a next hop! */
    if ((subnet == 0) && (next_hop == FW_ROUTING_NONEXTHOP)) {
        return ROUTING_ERR_INVALID_ROUTE;
    } else if (table->size >= table->capacity) {
        return ROUTING_ERR_FULL;
    }

    for (uint16_t i = 0; i < table->size; i++) {
        fw_routing_entry_t *entry = table->entries + i;

        /* One rule applies to a larger subnet than the other */
        if (subnet != entry->subnet) {
            continue;
        }

        /* Rules apply to different subnets */
        if ((subnet_mask(subnet) & ip) != entry->ip) {
            continue;
        }

        /* There is a clash! */
        if ((interface == entry->interface) && (next_hop == entry->next_hop)) {
            return ROUTING_ERR_DUPLICATE;
        } else {
            return ROUTING_ERR_CLASH;
        }
    }

    fw_routing_entry_t *empty_slot = table->entries + table->size;
    empty_slot->interface = interface;
    empty_slot->ip = subnet_mask(subnet) & ip;
    empty_slot->subnet = subnet;
    empty_slot->next_hop = next_hop;
    table->size++;

    return ROUTING_ERR_OKAY;
}

/**
 * Remove a route from the routing table.
 *
 * @param table address of routing table.
 * @param route_id ID of route to remove.
 *
 * @return error status of operation.
 */
static fw_routing_err_t fw_routing_table_remove_route(fw_routing_table_t *table,
                                                      uint16_t route_id)
{
    if (route_id >= table->size) {
        return ROUTING_ERR_INVALID_ID;
    }

    /* Shift everything left to delete this item */
    generic_array_shift(table->entries, sizeof(fw_routing_entry_t),
                        table->capacity,
                        route_id);
    table->size--;
    return ROUTING_ERR_OKAY;
}

/**
 * Initialise the routing table. Adds entry for external interface based on
 * external subnet.
 *
 * @param table address of routing table.
 * @param table_vaddr address of routing entries.
 * @param capacity capacity of routing table.
 * @param extern_ip IP address of external interface.
 * @param extern_subnet subnet bits of external interface.
 */
static void fw_routing_table_init(fw_routing_table_t **table,
                                  void *table_vaddr,
                                  uint16_t capacity,
                                  uint32_t extern_ip,
                                  uint8_t extern_subnet)
{
    *table = (fw_routing_table_t *)table_vaddr;
    (*table)->capacity = capacity;
    (*table)->size = 0;

    /* Add a route for external network */
    fw_routing_err_t err = fw_routing_table_add_route(*table,
                                           ROUTING_OUT_EXTERNAL,
                                                  extern_ip,
                                              extern_subnet,
                                            FW_ROUTING_NONEXTHOP);
    assert(err == ROUTING_ERR_OKAY);
}
