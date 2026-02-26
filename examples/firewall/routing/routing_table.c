/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <lions/firewall/array_functions.h>
#include <lions/firewall/common.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/routing.h>

const char *fw_routing_err_str[] = { "Ok.",
                                     "Out of memory error.",
                                     "Duplicate entry.",
                                     "Clashing entry.",
                                     "Invalid child node.",
                                     "Invalid route ID.",
                                     "Invalid route values." };

fw_routing_err_t fw_routing_find_route(fw_routing_table_t *table, uint32_t *ip, uint8_t *interface)
{
    uint8_t num_lookups = 0;
    while (num_lookups < FW_ROUTING_MAX_RECURSION) {
        fw_routing_entry_t *match = NULL;
        for (uint16_t i = 0; i < table->size; i++) {
            fw_routing_entry_t *entry = table->entries + i;

            /* ip is part of subnet */
            if ((subnet_mask(entry->subnet) & *ip) == entry->ip) {

                /* Current match is stronger */
                if (match != NULL && match->subnet > entry->subnet) {
                    continue;
                }

                match = entry;
            }
        }

        if (match == NULL) {
            /* No route found */
            *ip = FW_ROUTING_NONEXTHOP;
            return ROUTING_ERR_OKAY;
        }

        if (match->next_hop == FW_ROUTING_NONEXTHOP) {
            *interface = match->interface;
            return ROUTING_ERR_OKAY;
        }

        *ip = match->next_hop;
        num_lookups++;
    }

    /* No route found */
    *ip = FW_ROUTING_NONEXTHOP;
    return ROUTING_ERR_OKAY;
}

fw_routing_err_t fw_routing_table_add_route(fw_routing_table_t *table, uint8_t interface, uint32_t ip,
                                                   uint8_t subnet, uint32_t next_hop)
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

fw_routing_err_t fw_routing_table_remove_route(fw_routing_table_t *table, uint16_t route_id)
{
    if (route_id >= table->size) {
        return ROUTING_ERR_INVALID_ID;
    }

    /* Shift everything left to delete this item */
    generic_array_shift(table->entries, sizeof(fw_routing_entry_t), table->capacity, route_id);
    table->size--;
    return ROUTING_ERR_OKAY;
}

void fw_routing_table_init(fw_routing_table_t **table, void *table_vaddr, uint16_t capacity,
                                  fw_routing_entry_t *initial_routes, uint8_t num_initial_routes)
{
    *table = (fw_routing_table_t *)table_vaddr;
    (*table)->capacity = capacity;
    (*table)->size = 0;

    for (uint8_t r = 0; r < num_initial_routes; r++) {
        fw_routing_err_t err = fw_routing_table_add_route(*table, initial_routes[r].interface,
                                                          initial_routes[r].ip, initial_routes[r].subnet,
                                                          initial_routes[r].next_hop);

        assert(err == ROUTING_ERR_OKAY);
    }
}
