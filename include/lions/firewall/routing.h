/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
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

extern const char *fw_routing_err_str[];

/* PP call parameters for webserver to call router and update routing table */
#define FW_ADD_ROUTE 0
#define FW_DEL_ROUTE 1

typedef enum {
    ROUTER_ARG_ROUTE_ID = 0,
    ROUTER_ARG_IP,
    ROUTER_ARG_SUBNET,
    ROUTER_ARG_NEXT_HOP,
    ROUTER_ARG_INTERFACE
} fw_router_args_t;

typedef enum { ROUTER_RET_ERR = 0 } fw_router_ret_args_t;

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

/* packet waiting node used to store outgoing packets for an interface before
MAC address has been resolved */
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
    fw_buff_desc_t buffer;
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
void pkt_waiting_init(pkts_waiting_t *pkts_waiting, void *packets, int16_t capacity);

/**
 * Check if the packet waiting queue is full.
 *
 * @param pkts_waiting address of packets waiting structure.
 *
 * @return whether packet waiting queue is full.
 */
bool pkt_waiting_full(pkts_waiting_t *pkts_waiting);

/**
 * Find matching ip packet waiting node in packet waiting list.
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param ip ip adress to match with.
 *
 * @return address of matching packet waiting root node or NULL if no match.
 */
pkt_waiting_node_t *pkt_waiting_find_node(pkts_waiting_t *pkts_waiting, uint32_t ip);

/**
 * Return the next child node, assumes child node is valid!
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param node parent node.
 *
 * @return address of child node.
 */
pkt_waiting_node_t *pkts_waiting_next_child(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *node);

/**
 * Add a child node to a root waiting node. Node passed must be a root node!
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param root root node.
 * @param buffer buffer holding outgoing packet to be stored in new node.
 *
 * @return error status of operation.
 */
fw_routing_err_t pkt_waiting_push_child(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *root,
                                               fw_buff_desc_t buffer);

/**
 * Add a new root node to IP packet list. Assumes no valid root node for IP.
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param ip IP address of outgoing packet stored in new node.
 * @param buffer buffer holding outgoing packet to be stored in new node.
 *
 * @return error status of operation.
 */
fw_routing_err_t pkt_waiting_push(pkts_waiting_t *pkts_waiting, uint32_t ip, fw_buff_desc_t buffer);

/**
 * Free a node and all its children. Must pass a root node!
 *
 * @param pkts_waiting address of packets waiting structure.
 * @param root root node to free.
 *
 * @return error status of operation.
 */
fw_routing_err_t pkts_waiting_free_parent(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *root);

/**
 * Find next hop for destination IP. Maximum recursion limit to prevent infinite
 * looping.
 *
 * @param table address of routing table.
 * @param ip address of destination IP, modified to hold the IP of the next hop,
 * or FW_ROUTING_NONEXTHOP if it is unreachable.
 * @param interface address of the interface traffic should be routed out.
 *
 * @return error status of operation.
 */
fw_routing_err_t fw_routing_find_route(fw_routing_table_t *table, uint32_t *ip, uint8_t *interface);

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
fw_routing_err_t fw_routing_table_add_route(fw_routing_table_t *table, uint8_t interface, uint32_t ip,
                                                   uint8_t subnet, uint32_t next_hop);

/**
 * Remove a route from the routing table.
 *
 * @param table address of routing table.
 * @param route_id ID of route to remove.
 *
 * @return error status of operation.
 */
fw_routing_err_t fw_routing_table_remove_route(fw_routing_table_t *table, uint16_t route_id);

/**
 * Initialise the routing table.
 *
 * @param table address of routing table.
 * @param table_vaddr address of routing entries.
 * @param capacity capacity of routing table.
 * @param initial_routes address of initial route table.
 * @param num_initial_routes number of initial routes.
 */
void fw_routing_table_init(fw_routing_table_t **table, void *table_vaddr, uint16_t capacity,
                                  fw_routing_entry_t *initial_routes, uint8_t num_initial_routes);
