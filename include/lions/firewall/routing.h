#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lions/firewall/array_functions.h>
#include <lions/firewall/common.h>
#include <lions/firewall/queue.h>

/* No next hop */
#define FW_ROUTING_NONEXTHOP 0

/* Maximum number of recursive calls that find route can make */
#define FW_ROUTING_MAX_RECURSION 3

/* Routing internal errors */
typedef enum {
    ROUTING_ERR_OKAY = 0,       /* No error */
	ROUTING_ERR_FULL,           /* Data structure is full */
	ROUTING_ERR_DUPLICATE,	    /* Duplicate entry exists */
    ROUTING_ERR_CLASH,          /* Entry clashes with existing entry */
    ROUTING_ERR_INVALID_CHILD,  /* Child node IP does not match parent node IP */
    ROUTING_ERR_INVALID_ID,     /* Node does not exist */
    ROUTING_ERR_INVALID_ROUTE   /* Specified route is invalid */ 
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

/* Routing interfaces */
typedef enum {
    ROUTING_OUT_NONE = 0, /* do not transmit */
    ROUTING_OUT_EXTERNAL, /* transmit out NIC */
	ROUTING_OUT_SELF /* transmit within the system */
} fw_routing_interfaces_t;

/* PP call parameters for webserver to call routers */
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
    fw_routing_interfaces_t interface; /* interface subnet traffic should be transmitted through */
    uint32_t ip; /* ip address of destination subnet */
    uint8_t subnet; /* number of bits in subnet mask */
    uint32_t next_hop; /* ip addr of next hop */
} fw_routing_entry_t;

typedef struct routing_table {
    uint16_t capacity; /* capacity of table */
    uint16_t size;
    fw_routing_entry_t entries[]; /* subnet entries */
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
    uint16_t size;  /* number of nodes in use */
    uint16_t length;  /* number of parent nodes */
    uint16_t waiting_head;
    uint16_t waiting_tail;
    uint16_t free_head;
} pkts_waiting_t;

/* Initialise packet waiting structure */
void pkt_waiting_init(pkts_waiting_t *pkts_waiting, void *packets, uint16_t capacity) {
    pkts_waiting->packets = (pkt_waiting_node_t *)packets;
    pkts_waiting->capacity = capacity;
    for (uint16_t i = 0; i < pkts_waiting->capacity; i++) {
        pkt_waiting_node_t *node = pkts_waiting->packets + i;
        /* Free list only maintains next pointers */
        node->next_ip = i + 1;
    }
}

/* Check if the packet waiting queue is full */
bool pkt_waiting_full(pkts_waiting_t *pkts_waiting) {
    return pkts_waiting->size == pkts_waiting->capacity;
}

/* Find matching ip packet waiting node in packet waiting list */
pkt_waiting_node_t *pkt_waiting_find_node(pkts_waiting_t *pkts_waiting, uint32_t ip) {
    pkt_waiting_node_t *node = pkts_waiting->packets + pkts_waiting->waiting_head;
    for (uint16_t i = 0; i < pkts_waiting->length; i++) {
        if (node->ip == ip) {
            return node;
        }
        node = pkts_waiting->packets + node->next_ip;
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
    pkt_waiting_node_t *last_child = parent;
    for (uint16_t i = 0; i < parent->num_children; i++) {
        last_child = pkts_waiting_next_child(pkts_waiting, last_child);
    }
    last_child->next_child = new_idx;

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

    /* Update values */
    new_node->ip = ip;
    new_node->buffer = buffer;
    new_node->num_children = 0;

    /* Update pointers */
    pkts_waiting->free_head = new_node->next_ip;
    /* If this is not the first node */
    if (pkts_waiting->length) {
        uint16_t head_idx = pkts_waiting->waiting_head;
        pkt_waiting_node_t *head_node = pkts_waiting->packets + head_idx;

        new_node->next_ip = head_idx;
        head_node->prev_ip = new_idx;
    } else {
        pkts_waiting->waiting_tail = new_idx;
    }
    pkts_waiting->waiting_head = new_idx;

    /* Update counts */
    pkts_waiting->length++;
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
    pkts_waiting->free_head = parent_idx;

    /* Update counts */
    pkts_waiting->length--;
    pkts_waiting->size--;

    return ROUTING_ERR_OKAY;
}

/* Find next hop for destination IP. Maximum recursion limit to prevent infinite looping. */
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
        *interface = ROUTING_OUT_NONE;
    } else if (match->interface == ROUTING_OUT_SELF) {
        *interface = ROUTING_OUT_SELF;
    } else if (match->interface == ROUTING_OUT_EXTERNAL && match->next_hop == FW_ROUTING_NONEXTHOP) {
        *next_hop = ip;
        *interface = ROUTING_OUT_EXTERNAL;
    } else if (match->interface == ROUTING_OUT_EXTERNAL && match->next_hop != FW_ROUTING_NONEXTHOP) {
        num_calls ++;
        if (num_calls == FW_ROUTING_MAX_RECURSION) {
            /* Find route has hit recursive call limit, ip unreachable. */
            *interface = ROUTING_OUT_NONE;
            return ROUTING_ERR_OKAY;
        }
        fw_routing_err_t err = fw_routing_find_route(table, match->next_hop, next_hop, interface, num_calls);
        return err;
    }

    return ROUTING_ERR_OKAY;
}

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

static fw_routing_err_t fw_routing_table_remove_route(fw_routing_table_t *table, uint16_t route_id)
{
    if (route_id >= table->size) {
        return ROUTING_ERR_INVALID_ID;
    }

    /* Shift everything left to delete this item */
    generic_array_shift(table->entries, sizeof(fw_routing_entry_t), table->capacity, route_id);
    table->size--;
    return ROUTING_ERR_OKAY;
}

static void fw_routing_table_init(fw_routing_table_t **table,
                                  void *table_vaddr, 
                                  uint16_t capacity,
                                  uint32_t extern_ip,
                                  uint8_t extern_subnet)
{
    *table = table_vaddr;
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
