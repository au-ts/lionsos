/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <sddf/network/util.h>
#include <sddf/resources/common.h>
#include <lions/firewall/common.h>
#include <lions/firewall/array_functions.h>

/* The default action of a filter is always stored at index 0 of the rule table,
and has a fixed rule ID of 0 */
#define DEFAULT_ACTION_IDX 0
#define DEFAULT_ACTION_RULE_ID 0

typedef enum {
    /* no error */
    FILTER_ERR_OKAY = 0,
    /* data structure is full */
    FILTER_ERR_FULL,
    /* duplicate entry exists */
    FILTER_ERR_DUPLICATE,
    /* entry clashes with existing entry */
    FILTER_ERR_CLASH,
    /* rule id does not point to a valid entry, or is the default action rule id */
    FILTER_ERR_INVALID_RULE_ID
} fw_filter_err_t;

static const char *fw_filter_err_str[] = { "Ok.", "Out of memory error.", "Duplicate entry.", "Clashing entry.",
                                           "Invalid rule ID." };

typedef enum {
    /* allow traffic */
    FILTER_ACT_ALLOW = 1,
    /* drop traffic */
    FILTER_ACT_DROP = 2,
    /* allow traffic, and additionally any return traffic */
    FILTER_ACT_CONNECT = 3,
    /* traffic is return traffic from a connect rule */
    FILTER_ACT_ESTABLISHED
} fw_action_t;

static const char *fw_filter_action_str[] = { "No rule", "Allow", "Drop", "Connect", "Established" };

typedef struct fw_rule {
    /* action to be applied to traffic matching rule */
    uint8_t action;
    /* source IP */
    uint32_t src_ip;
    /* destination IP */
    uint32_t dst_ip;
    /* source port number */
    uint16_t src_port;
    /* destination port number */
    uint16_t dst_port;
    /* source subnet, 0 is any IP */
    uint8_t src_subnet;
    /* destination subnet, 0 is any IP */
    uint8_t dst_subnet;
    /* rule applies to any source port */
    bool src_port_any;
    /* rule applies to any destination port */
    bool dst_port_any;
    /* rule id assigned */
    uint16_t rule_id;
} fw_rule_t;

/**
 * Instances are created by filters if traffic matches with a connect rule.
 * If this is the case, return traffic should be permitted also, thus the
 * filter will create an instance in shared memory so the matching filter
 * can search for and identify return traffic.
 */
typedef struct fw_instance {
    /* source ip of traffic */
    uint32_t src_ip;
    /* destination ip of traffic */
    uint32_t dst_ip;
    /* source port of traffic */
    uint16_t src_port;
    /* destination port of traffic */
    uint16_t dst_port;
    /* ID of the rule this instance was created from. Allows instances
    to be removed upon rule removal */
    uint16_t rule_id;
} fw_instance_t;

typedef struct fw_instances_table {
    uint16_t size;
    fw_instance_t instances[];
} fw_instances_table_t;

typedef struct fw_rule_table {
    uint16_t size;
    fw_rule_t rules[];
} fw_rule_table_t;

typedef struct fw_rule_id_bitmap {
    uint16_t last_allocated_rule_id;
    uint64_t id_bitmap[];
} fw_rule_id_bitmap_t;

typedef struct fw_filter_state {
    /* filter rules */
    fw_rule_table_t *rule_table;
    /* capacity of filter rules */
    uint16_t rules_capacity;
    /* bitmap to track filter rule ids */
    fw_rule_id_bitmap_t *rule_id_bitmap;
    /* instances created by this filter,
    to be searched by neighbour filter */
    fw_instances_table_t *internal_instances_table;
    /* instances created by neighbour filter,
    to be searched by this filter */
    fw_instances_table_t *external_instances_table[FW_MAX_INTERFACES];
    /* capacity of both instance tables */
    uint16_t instances_capacity;
    /* number of interfaces */
    uint8_t num_interfaces;
} fw_filter_state_t;

/* PP call parameters for webserver to call filters and update rules */
#define FW_SET_DEFAULT_ACTION 0
#define FW_ADD_RULE 1
#define FW_DEL_RULE 2

typedef enum {
    FILTER_ARG_ACTION = 0,
    FILTER_ARG_RULE_ID = 1,
    FILTER_ARG_SRC_IP = 2,
    FILTER_ARG_SRC_PORT = 3,
    FILTER_ARG_DST_IP = 4,
    FILTER_ARG_DST_PORT = 5,
    FILTER_ARG_SRC_SUBNET = 6,
    FILTER_ARG_DST_SUBNET = 7,
    FILTER_ARG_SRC_ANY_PORT = 8,
    FILTER_ARG_DST_ANY_PORT = 9
} fw_args_t;

typedef enum { FILTER_RET_ERR = 0, FILTER_RET_RULE_ID = 1 } fw_ret_args_t;

/* The rule ID allocation bitmap uses blocks of 64 bits */
#define RULE_ID_BITMAP_BLK_SIZE 64

/**
 * Reserve an unused rule ID from the bitmap and mark it as allocated. Allocates
 * circularly starting from the last allocated ID position.
 *
 * @param state pointer to the filter state.
 * @param rule_id pointer to return the allocated rule ID.
 *
 * @return FILTER_ERR_OKAY if ID allocated successfully, FILTER_ERR_FULL if no
 * IDs available.
 */
static fw_filter_err_t rules_reserve_id(fw_filter_state_t *state, uint16_t *rule_id)
{
    if (state->rule_table->size >= state->rules_capacity) {
        return FILTER_ERR_FULL;
    }

    uint16_t id_to_reserve = DEFAULT_ACTION_RULE_ID;
    for (uint16_t i = 0; i < state->rules_capacity; i++) {
        uint16_t id_to_check = (state->rule_id_bitmap->last_allocated_rule_id + 1 + i) % state->rules_capacity;

        uint16_t block_idx = id_to_check / RULE_ID_BITMAP_BLK_SIZE;
        uint64_t mask = 1ULL << (id_to_check % RULE_ID_BITMAP_BLK_SIZE);

        if (!(state->rule_id_bitmap->id_bitmap[block_idx] & mask)) {
            state->rule_id_bitmap->id_bitmap[block_idx] |= mask;
            state->rule_id_bitmap->last_allocated_rule_id = id_to_check;
            id_to_reserve = id_to_check;
            break;
        }
    }

    assert(id_to_reserve != DEFAULT_ACTION_RULE_ID);
    *rule_id = id_to_reserve;

    return FILTER_ERR_OKAY;
}

/**
 * Free a previously allocated rule ID by clearing its bit in the bitmap. The
 * default rule ID cannot be freed.
 *
 * @param state pointer to the filter state
 * @param rule_id rule ID to free.
 *
 * @return FILTER_ERR_OKAY if ID was allocated and freed successfully, error
 * otherwise.
 */
static fw_filter_err_t rules_free_id(fw_filter_state_t *state, uint16_t rule_id)
{
    if (rule_id == DEFAULT_ACTION_RULE_ID || rule_id >= state->rules_capacity) {
        return FILTER_ERR_INVALID_RULE_ID;
    }

    uint16_t block_idx = rule_id / RULE_ID_BITMAP_BLK_SIZE;
    uint64_t mask = 1ULL << (rule_id % RULE_ID_BITMAP_BLK_SIZE);

    if (!(state->rule_id_bitmap->id_bitmap[block_idx] & mask)) {
        return FILTER_ERR_INVALID_RULE_ID;
    }

    state->rule_id_bitmap->id_bitmap[block_idx] &= ~mask;
    return FILTER_ERR_OKAY;
}

/**
 * Add a filtering rule.
 *
 * @param state address of filter state.
 * @param src_ip source ip of traffic rule applies to.
 * @param src_port source port of traffic rule applies to.
 * @param dst_ip destination ip of traffic rule applies to.
 * @param dst_port destination port of traffic rule applies to.
 * @param src_subnet subnet bits of source ip traffic rule applies to.
 * @param dst_subnet subnet bits of destination ip traffic rule applies to.
 * @param src_port_any whether rule applies to any source port.
 * @param dst_port_any whether rule applies to any destination port.
 * @param action action to be applied to traffic matching rule.
 * @param rule_id address of rule id to be set upon successful rule creation.
 *
 * @return error status.
 */
static inline fw_filter_err_t fw_filter_add_rule(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                                 uint32_t dst_ip, uint16_t dst_port, uint8_t src_subnet,
                                                 uint8_t dst_subnet, bool src_port_any, bool dst_port_any,
                                                 fw_action_t action, uint16_t *rule_id)
{
    if (state->rule_table->size >= state->rules_capacity) {
        return FILTER_ERR_FULL;
    }

    for (uint16_t i = 0; i < state->rule_table->size; i++) {
        fw_rule_t *rule = (fw_rule_t *)(state->rule_table->rules + i);

        /* Check that this entry won't cause clashes */

        /* One rule applies to any src port, one applies to a specific src port */
        if ((src_port_any && !rule->src_port_any) || (!src_port_any && rule->src_port_any)) {
            continue;
        }

        /* One rule applies to any dst port, one applies to a specific dst port */
        if ((dst_port_any && !rule->dst_port_any) || (!dst_port_any && rule->dst_port_any)) {
            continue;
        }

        /* One rule applies to one port, one applies to another */
        if (src_port != rule->src_port || dst_port != rule->dst_port) {
            continue;
        }

        /* One rule applies to a larger subnet than the other */
        if (src_subnet != rule->src_subnet || dst_subnet != rule->dst_subnet) {
            continue;
        }

        /* Rules apply to different source subnets */
        if ((subnet_mask(src_subnet) & src_ip) != (subnet_mask(rule->src_subnet) & rule->src_ip)) {
            continue;
        }

        /* Rules apply to different destination subnets */
        if ((subnet_mask(dst_subnet) & dst_ip) != (subnet_mask(rule->dst_subnet) & rule->dst_ip)) {
            continue;
        }

        /* There is a clash! */
        if (action == rule->action) {
            return FILTER_ERR_DUPLICATE;
        } else {
            return FILTER_ERR_CLASH;
        }
    }

    fw_rule_t *empty_slot = state->rule_table->rules + state->rule_table->size;
    empty_slot->src_ip = subnet_mask(src_subnet) & src_ip;
    empty_slot->src_port = src_port;
    empty_slot->dst_ip = subnet_mask(dst_subnet) & dst_ip;
    empty_slot->dst_port = dst_port;
    empty_slot->src_subnet = src_subnet;
    empty_slot->dst_subnet = dst_subnet;
    empty_slot->src_port_any = src_port_any;
    empty_slot->dst_port_any = dst_port_any;
    empty_slot->action = action;

    assert(rules_reserve_id(state, rule_id) == FILTER_ERR_OKAY);

    empty_slot->rule_id = *rule_id;
    state->rule_table->size++;
    return FILTER_ERR_OKAY;
}

/**
 * Initialise filter state.
 *
 * @param state address of filter state.
 * @param rules address of rules table.
 * @param rule_id_bitmap address of rule id allocation bitmap.
 * @param rules_capacity capacity of rules table.
 * @param internal_instances address of internal instances.
 * @param external_instances address of external instances.
 * @param instances_capacity capacity of instance tables.
 * @param initial_rules array of initial rules to insert.
 * @param num_rules number of initial rules.
 */
static inline void fw_filter_state_init(fw_filter_state_t *state, void *rules, void *rule_id_bitmap,
                                        uint16_t rules_capacity, void *internal_instances,
                                        region_resource_t *external_instances, uint16_t instances_capacity,
                                        fw_rule_t *initial_rules, uint8_t num_rules, uint8_t num_interfaces)
{
    state->rule_table = (fw_rule_table_t *)rules;
    state->rules_capacity = rules_capacity;
    state->rule_id_bitmap = (fw_rule_id_bitmap_t *)rule_id_bitmap;
    state->instances_capacity = instances_capacity;
    state->internal_instances_table = (fw_instances_table_t *)internal_instances;
    state->num_interfaces = num_interfaces;
    /* Populate the array of possible external instance tables */
    for (size_t i = 0; i < num_interfaces; i++) {
        state->external_instances_table[i] = (fw_instances_table_t *)external_instances[i].vaddr;
    }

    /* Allocate the default action rule ID for the default action */
    uint16_t default_block_idx = DEFAULT_ACTION_RULE_ID / RULE_ID_BITMAP_BLK_SIZE;
    uint64_t default_mask = 1ULL << (DEFAULT_ACTION_RULE_ID % RULE_ID_BITMAP_BLK_SIZE);

    /* No other rules should exist at this point */
    assert((state->rule_id_bitmap->id_bitmap[default_block_idx] & default_mask) == 0);
    assert(state->rule_table->size == 0);

    /* First rule must be the default rule */
    assert(num_rules >= 1);
    assert(initial_rules[DEFAULT_ACTION_IDX].src_subnet == 0 && initial_rules[DEFAULT_ACTION_IDX].src_port_any);
    assert(initial_rules[DEFAULT_ACTION_IDX].dst_subnet == 0 && initial_rules[DEFAULT_ACTION_IDX].dst_port_any);
    assert(initial_rules[DEFAULT_ACTION_IDX].rule_id == DEFAULT_ACTION_RULE_ID);

    state->rule_id_bitmap->id_bitmap[default_block_idx] |= default_mask;
    state->rule_id_bitmap->last_allocated_rule_id = DEFAULT_ACTION_RULE_ID;

    state->rule_table->rules[DEFAULT_ACTION_IDX] = initial_rules[DEFAULT_ACTION_IDX];
    state->rule_table->size++;

    for (uint8_t r = 1; r < num_rules; r++) {
        fw_filter_err_t err = fw_filter_add_rule(state, initial_rules[r].src_ip, initial_rules[r].src_port,
                                                 initial_rules[r].dst_ip, initial_rules[r].dst_port,
                                                 initial_rules[r].src_subnet, initial_rules[r].dst_subnet,
                                                 initial_rules[r].src_port_any, initial_rules[r].dst_port_any,
                                                 initial_rules[r].action, &initial_rules[r].rule_id);
        assert(err == FILTER_ERR_OKAY);
    }
}

/**
 * Create an instance. To be used after traffic matches with a connect rule,
 * allowing neighbour filter to permit return traffic.
 *
 * @param state address of filter state.
 * @param src_ip source ip of instance traffic.
 * @param src_port source port of instance traffic.
 * @param dst_ip destination ip of instance traffic.
 * @param dst_port destination port of instance traffic.
 * @param default_action whether connect rule was matched via filter's default action.
 * @param rule_id id of connect rule.
 *
 * @return error status.
 */
static inline fw_filter_err_t fw_filter_add_instance(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                                     uint32_t dst_ip, uint16_t dst_port, uint16_t rule_id)
{
    if (state->internal_instances_table->size >= state->instances_capacity) {
        return FILTER_ERR_FULL;
    }

    for (uint16_t i = 0; i < state->internal_instances_table->size; i++) {
        fw_instance_t *instance = state->internal_instances_table->instances + i;

        /* Connection has already been established */
        if (instance->rule_id == rule_id && instance->src_ip == src_ip && instance->src_port == src_port
            && instance->dst_ip == dst_ip && instance->dst_port == dst_port) {
            return FILTER_ERR_DUPLICATE;
        }
    }

    fw_instance_t *empty_slot = state->internal_instances_table->instances + state->internal_instances_table->size;
    empty_slot->rule_id = rule_id;
    empty_slot->src_ip = src_ip;
    empty_slot->src_port = src_port;
    empty_slot->dst_ip = dst_ip;
    empty_slot->dst_port = dst_port;
    state->internal_instances_table->size++;

    return FILTER_ERR_OKAY;
}

/**
 * Find the filter action to be applied for a given source and destination ip
 * and port number. First external instances are checked so that return traffic
 * may be permitted. If traffic is not return traffic from a neighbour filter's
 * connection, the most specific matching filter rule is returned.
 *
 * @param state address of filter state.
 * @param src_ip source ip to match.
 * @param src_port source port to match.
 * @param dst_ip destination ip to match.
 * @param dst_port destination port to match.
 * @param rule_id id of matching rule. Unmodified if no match.
 *
 * @return filter action to be applied. None is returned if no match is found.
 */
static inline fw_action_t fw_filter_find_action(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                                uint32_t dst_ip, uint16_t dst_port, uint16_t *rule_id)
{
    /* First check external instances */
    for (size_t iface = 0; iface < state->num_interfaces; iface++) {
        for (uint16_t i = 0; i < state->external_instances_table[iface]->size; i++) {
            fw_instance_t *instance = state->external_instances_table[iface]->instances + i;

            if (instance->src_port != dst_port || instance->dst_port != src_port) {
                continue;
            }

            if (instance->src_ip != dst_ip || instance->dst_ip != src_ip) {
                continue;
            }

            *rule_id = instance->rule_id;
            return FILTER_ACT_ESTABLISHED;
        }
    }

    /* Check rules for best match otherwise we match with the default rule */
    fw_rule_t *match = &state->rule_table->rules[DEFAULT_ACTION_IDX];
    for (uint16_t i = DEFAULT_ACTION_IDX + 1; i < state->rule_table->size; i++) {
        fw_rule_t *rule = state->rule_table->rules + i;

        /* Check port numbers first */
        if ((!rule->src_port_any && rule->src_port != src_port)
            || (!rule->dst_port_any && rule->dst_port != dst_port)) {
            continue;
        }

        /* Match on src addr first */
        if ((subnet_mask(rule->src_subnet) & src_ip) != (subnet_mask(rule->src_subnet) & rule->src_ip)) {
            continue;
        }

        /* Match on src addr first */
        if ((subnet_mask(rule->dst_subnet) & dst_ip) != (subnet_mask(rule->dst_subnet) & rule->dst_ip)) {
            continue;
        }

        /* This if the first match we've found */
        if (match == NULL) {
            match = rule;
        }

        /* We give priority to source matches over destination matches */
        if (rule->src_subnet == match->src_subnet) {
            if (rule->dst_subnet == match->dst_subnet) {
                if (rule->src_port_any == match->src_port_any) {
                    if (!rule->dst_port_any && match->dst_port_any) {
                        match = rule; /* destination port number is a stronger match */
                    }
                } else if (!rule->src_port_any && match->src_port_any) {
                    match = rule; /* source port number is a stronger match */
                }
            } else if (rule->dst_subnet > match->dst_subnet) { /* destination subnet is a longer match */
                match = rule;
            }
        } else if (rule->src_subnet > match->src_subnet) {
            match = rule; /* source subnet is a longer match */
        }
    }

    *rule_id = match->rule_id;
    return (fw_action_t)match->action;
}

/**
 * Remove instances associated with a rule. To be used when a rule is
 * deleted or default action is changed.
 *
 * @param state address of filter state.
 * @param rule_id ID of rule that has been deleted.
 *
 * @return error status.
 */
static fw_filter_err_t fw_filter_remove_instances(fw_filter_state_t *state, uint16_t rule_id)
{
    uint16_t i = 0;
    while (i < state->internal_instances_table->size) {
        fw_instance_t *instance = state->internal_instances_table->instances + i;

        if (rule_id != instance->rule_id) {
            i++;
            continue;
        }

        state->internal_instances_table->instances[i] =
            state->internal_instances_table->instances[state->internal_instances_table->size - 1];
        state->internal_instances_table->size--;
    }

    return FILTER_ERR_OKAY;
}

/**
 * Update filter's default action.
 *
 * @param state address of filter state.
 * @param new_action new default action.
 *
 * @return error status.
 */
static inline fw_filter_err_t fw_filter_update_default_action(fw_filter_state_t *state, fw_action_t new_action)
{
    fw_action_t old_action = state->rule_table->rules[DEFAULT_ACTION_IDX].action;
    if (new_action == old_action) {
        return FILTER_ERR_OKAY;
    }

    if (old_action == FILTER_ACT_CONNECT) {
        fw_filter_err_t err = fw_filter_remove_instances(state, DEFAULT_ACTION_RULE_ID);
        assert(err == FILTER_ERR_OKAY);
    }

    state->rule_table->rules[DEFAULT_ACTION_IDX].action = new_action;

    return FILTER_ERR_OKAY;
}

/**
 * Remove a filter rule.
 *
 * @param state address of filter state.
 * @param rule_id ID of rule to be deleted.
 *
 * @return error status.
 */
static inline fw_filter_err_t fw_filter_remove_rule(fw_filter_state_t *state, uint16_t rule_id)
{
    fw_filter_err_t err = rules_free_id(state, rule_id);
    if (err != FILTER_ERR_OKAY) {
        return err;
    }

    fw_rule_t *rule = NULL;
    for (uint16_t i = DEFAULT_ACTION_IDX + 1; i < state->rule_table->size; i++) {
        if (state->rule_table->rules[i].rule_id == rule_id) {
            rule = state->rule_table->rules + i;
            break;
        }
    }

    assert(rule != NULL);

    if ((fw_action_t)rule->action == FILTER_ACT_CONNECT) {
        assert(fw_filter_remove_instances(state, rule_id) == FILTER_ERR_OKAY);
    }

    generic_array_shift(state->rule_table->rules, sizeof(fw_rule_t), state->rule_table->size,
                        rule - state->rule_table->rules);
    state->rule_table->size--;
    return FILTER_ERR_OKAY;
}
