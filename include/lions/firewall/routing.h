#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <lions/firewall/common.h>
#include <lions/firewall/queue.h>

/* Routing internal errors */
typedef enum {
    ROUTING_ERR_OKAY = 0,       /* No error */
	ROUTING_ERR_FULL,           /* Data structure is full */
	ROUTING_ERR_DUPLICATE,	    /* Duplicate entry exists */
    ROUTING_ERR_CLASH,          /* Entry clashes with existing entry */
    ROUTING_ERR_INVALID_CHILD,  /* Child node IP does not match parent node IP */
    ROUTING_ERR_INVALID_ID      /* Node does not exist */
} fw_routing_err_t;

static const char *fw_routing_err_str[] = {
    "Ok.",
    "Out of memory error.",
    "Duplicate entry.",
    "Clashing entry.",
    "Invalid child node.",
    "Invalid rule ID."
};

/* Routing interfaces */
typedef enum {
    ROUTING_OUT_EXTERNAL = 0, /* transmit out NIC */
	ROUTING_OUT_INTERNAL /* transmit within the system */
} fw_routing_out_interfaces_t;

/* PP call parameters for webserver to call routers */
#define FW_ADD_ROUTE 0
#define FW_DEL_ROUTE 1

typedef enum {
    ROUTER_ARG_ROUTE_ID = 0,
    ROUTER_ARG_IP,
    ROUTER_ARG_SUBNET,
    ROUTER_ARG_NEXT_HOP,
    ROUTER_ARG_NUM_HOPS
} fw_router_args_t;

typedef enum {
    ROUTER_RET_ERR = 0,
    ROUTER_RET_ROUTE_ID = 1
} fw_router_ret_args_t;

typedef struct routing_entry {
    bool valid;
    fw_routing_out_interfaces_t out_interface; /* queue subnet traffic should be transmitted through */
    uint16_t num_hops; /* minimum number of hops to destination */
    uint32_t ip; /* ip address of subnet */
    uint8_t subnet; /* number of bits in subnet mask */
    uint32_t next_hop; /* ip addr of next hop */
} fw_routing_entry_t;

typedef struct routing_table {
    fw_routing_entry_t *entries; /* subnet entries */
    fw_routing_entry_t default_route; /* default route if no matches are found */
    uint16_t capacity; /* capacity of table */
} fw_routing_table_t;

/* Node to track packets awaiting ARP requests */
typedef struct pkt_waiting_node {
    uint16_t next_ip;
    uint16_t prev_ip;
    uint16_t next_child;
    uint16_t num_children;
    uint32_t ip;
    fw_buff_desc_t buffer;
} pkt_waiting_node_t;

typedef struct pkts_waiting {
    pkt_waiting_node_t *packets;
    uint16_t capacity;
    uint16_t size;
    uint16_t waiting_head;
    uint16_t waiting_tail;
    uint16_t free_head;
} pkts_waiting_t;

/* Initialise packet waiting structure */
void pkt_waiting_init(pkts_waiting_t *pkts_waiting, void *packets, uint16_t capacity) {
    pkts_waiting->packets = (pkt_waiting_node_t *)packets;
    pkts_waiting->capacity = capacity;
    for (uint16_t i = 0; i < pkts_waiting->size; i++) {
        pkt_waiting_node_t *node = pkts_waiting->packets + i;
        node->next_ip = i + 1;
        node->prev_ip = i - 1;
    }
}

/* Check if the packet waiting queue is full */
bool pkt_waiting_full(pkts_waiting_t *pkts_waiting) {
    return pkts_waiting->size == pkts_waiting->capacity;
}

/* Find matching ip packet waiting node in packet waiting list */
pkt_waiting_node_t *pkt_waiting_find_node(pkts_waiting_t *pkts_waiting, uint32_t ip) {
    pkt_waiting_node_t *node = pkts_waiting->packets + pkts_waiting->waiting_head;
    for (uint16_t i = 0; i < pkts_waiting->size; i++) {
        if (node->ip == ip) {
            return node;
        }
    }

    return NULL;
}

/* Return the next child node */
pkt_waiting_node_t *pkts_waiting_next_child(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *node) {
    return pkts_waiting->packets + node->next_child;
}

/* Add a child node to a parent waiting node */
fw_routing_err_t pkt_waiting_push_child(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *parent, uint32_t ip, fw_buff_desc_t buffer) {
    if (pkt_waiting_full(pkts_waiting)) {
        return ROUTING_ERR_FULL;
    }

    if (parent->ip != ip) {
        return ROUTING_ERR_INVALID_CHILD;
    }

    uint16_t new_idx = pkts_waiting->free_head;
    pkt_waiting_node_t *new_node = pkts_waiting->packets + new_idx;

    /* Update values */
    new_node->ip = ip;
    new_node->buffer = buffer;
    new_node->num_children = 0;

    /* Update pointers */
    pkts_waiting->free_head = new_node->next_ip;
    pkt_waiting_node_t *next_child = parent;
    for (uint16_t i = 0; i < parent->num_children; i++) {
        next_child = pkts_waiting_next_child(pkts_waiting, next_child);
    }
    next_child->next_child = new_idx;

    /* Update counts */
    parent->num_children++;
    pkts_waiting->size++;

    return ROUTING_ERR_OKAY;
}

/* Add a node to IP packet list */
fw_routing_err_t pkt_waiting_push(pkts_waiting_t *pkts_waiting, uint32_t ip, fw_buff_desc_t buffer) {
    if (pkt_waiting_full(pkts_waiting)) {
        return ROUTING_ERR_FULL;
    }

    if (pkt_waiting_find_node(pkts_waiting, ip) != NULL) {
        return ROUTING_ERR_DUPLICATE;
    }

    uint16_t new_idx = pkts_waiting->free_head;
    pkt_waiting_node_t *new_node = pkts_waiting->packets + new_idx;
    uint16_t head_idx = pkts_waiting->waiting_head;
    pkt_waiting_node_t *head_node = pkts_waiting->packets + head_idx;

    /* Update values */
    new_node->ip = ip;
    new_node->buffer = buffer;
    new_node->num_children = 0;

    /* Update pointers */
    pkts_waiting->free_head = new_node->next_ip;
    new_node->next_ip = head_idx;
    head_node->prev_ip = new_idx;
    pkts_waiting->waiting_head = new_idx;
    if (pkts_waiting->size == 0) {
        pkts_waiting->waiting_tail = new_idx;
    }

    /* Update counts */
    pkts_waiting->size++;
    
    return ROUTING_ERR_OKAY;
}

/* Free a node and all child nodes. Must pass a parent node. */
fw_routing_err_t pkts_waiting_free_parent(pkts_waiting_t *pkts_waiting, pkt_waiting_node_t *parent) {
    /* First free children */
    uint16_t child_idx = parent->next_child;
    pkt_waiting_node_t *child = pkts_waiting_next_child(pkts_waiting, parent);
    for (uint16_t i = 0; i < parent->num_children; i++) {

        /* Add to free list */
        child->next_ip = pkts_waiting->free_head;
        pkts_waiting->free_head = child_idx;
        pkts_waiting->size--;

        /* Possibly free next child */
        child_idx = child->next_child;
        child = pkts_waiting_next_child(pkts_waiting, child);
    }

    /* Now free parent */
    pkt_waiting_node_t *prev_node = pkts_waiting->packets + parent->prev_ip;
    pkt_waiting_node_t *next_node = pkts_waiting->packets + parent->next_ip;

    /* Check if node is the head */
    uint16_t parent_idx = (uint16_t)(parent - pkts_waiting->packets);
    if (parent_idx == pkts_waiting->waiting_head) {
        pkts_waiting->waiting_head = parent->next_ip;
    } else {
        prev_node->next_ip = parent->next_ip;
    }

    /* Check if node is the tail */
    if (parent_idx == pkts_waiting->waiting_tail) {
        pkts_waiting->waiting_tail = parent->prev_ip;
    } else {
        next_node->prev_ip = parent->prev_ip;
    }

    /* Only maintain prev pointers for active list */
    parent->next_ip = pkts_waiting->free_head;
    pkts_waiting->free_head = parent - pkts_waiting->packets;
    pkts_waiting->size--;

    return ROUTING_ERR_OKAY;
}

static void fw_routing_table_init(fw_routing_table_t *table,
                                  fw_routing_entry_t default_route,
                                  void *entries, 
                                  uint16_t capacity)
{
    table->entries = (fw_routing_entry_t *)entries;
    table->default_route = default_route;
    table->capacity = capacity;
}

static uint16_t fw_routing_find_route(fw_routing_table_t *table,
                                      uint32_t ip,
                                      uint32_t *next_hop,
                                      fw_routing_out_interfaces_t *out_interface)
{
    fw_routing_entry_t *match = NULL;
    for (uint16_t i = 0; i < table->capacity; i++) {
        fw_routing_entry_t *entry = table->entries + i;
        if (!entry->valid) {
            continue;
        }

        if ((SUBNET_MASK(entry->subnet) & ip) == (SUBNET_MASK(entry->subnet) & entry->ip)) {
            /* ip is part of subnet */
            if (match == NULL) {
                match = entry;
                continue;
            }

            if (entry->subnet > match->subnet ||
                ((entry->subnet == match->subnet) && (entry->next_hop < match->num_hops))) {
                match = entry;
                continue;
            }
        }
    }

    if (match) {
        *next_hop = match->next_hop;
        *out_interface = match->out_interface;
        return match - table->entries;
    }

    /* Return the default route */
    *next_hop = ip;
    *out_interface = table->default_route.out_interface;

    return table->capacity;
}

static fw_routing_err_t fw_routing_table_add_route(fw_routing_table_t *table,
                                                   fw_routing_out_interfaces_t out_interface,
                                                   uint16_t num_hops,
                                                   uint32_t ip,
                                                   uint8_t subnet,
                                                   uint32_t next_hop,
                                                   uint16_t *route_id)
{
    fw_routing_entry_t *empty_slot = NULL;
    for (uint16_t i = 0; i < table->capacity; i++) {
        fw_routing_entry_t *entry = table->entries + i;

        if (!entry->valid) {
            if (empty_slot == NULL) {
                empty_slot = entry;
            }
            continue;
        }

        /* Check that this entry won't cause clash */
        if (num_hops != entry->num_hops) {
            continue;
        }

        /* One rule applies to a larger subnet than the other */
        if (subnet != entry->subnet) {
            continue;
        }

        /* Rules apply to different subnets */
        if ((SUBNET_MASK(subnet) & ip) != (SUBNET_MASK(entry->subnet) & entry->ip)) {
            continue;
        }

        /* There is a clash! */
        if ((out_interface == entry->out_interface) && (next_hop == entry->next_hop)) {
            return ROUTING_ERR_DUPLICATE;
        } else {
            return ROUTING_ERR_CLASH;
        }
    }

    if (empty_slot == NULL) {
        return ROUTING_ERR_FULL;
    }

    empty_slot->valid = true;
    empty_slot->out_interface = out_interface;
    empty_slot->num_hops = num_hops;
    empty_slot->ip = SUBNET_MASK(subnet) & ip;
    empty_slot->subnet = subnet;
    empty_slot->next_hop = next_hop;
    *route_id = empty_slot - table->entries;

    return ROUTING_ERR_OKAY;
}

static fw_routing_err_t fw_routing_table_remove_route(fw_routing_table_t *table, uint16_t route_id)
{
    fw_routing_entry_t *entry = table->entries + route_id;

    if (route_id >= table->capacity || !entry->valid) {
        return ROUTING_ERR_INVALID_ID;
    }

    entry->valid = false;

    return ROUTING_ERR_OKAY;
}
