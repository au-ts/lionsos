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
#include <lions/firewall/common.h>
#include <lions/firewall/array_functions.h>

#define MOD64 0x3F
#define BITMAP_BLOCK_SIZE 64
#define DEFAULT_RULE 0

typedef enum {
    /* no error */
    FILTER_ERR_OKAY = 0,
    /* data structure is full */
	FILTER_ERR_FULL,
    /* duplicate entry exists */
	FILTER_ERR_DUPLICATE,
    /* entry clashes with existing entry */
    FILTER_ERR_CLASH,
    /* rule id does not point to a valid entry */
    FILTER_ERR_INVALID_RULE_ID
} fw_filter_err_t;

static const char *fw_filter_err_str[] = {
    "Ok.",
    "Out of memory error.",
    "Duplicate entry.",
    "Clashing entry.",
    "Invalid rule ID."
};

typedef enum {
    /* no rule exists */
    FILTER_ACT_NONE,
    /* allow traffic */
	FILTER_ACT_ALLOW,
    /* drop traffic */
	FILTER_ACT_DROP,
    /* allow traffic, and additionally any return traffic */
    FILTER_ACT_CONNECT,
    /* traffic is return traffic from a connect rule */
    FILTER_ACT_ESTABLISHED
} fw_action_t;

static const char *fw_filter_action_str[] = {
    "No rule",
    "Allow",
    "Drop",
    "Connect",
    "Established"
};

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
    /* instance was created after matching with the filter's default action */
    bool default_action;
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
    fw_instances_table_t *external_instances_table;
    /* capacity of both instance tables */
    uint16_t instances_capacity;
    /* default action of filter to be applied
    if no other matches */
    fw_action_t default_action;
} fw_filter_state_t;

/**
 * Reserve an unused rule ID from the bitmap and mark it as allocated.
 * Searches circularly starting from the last allocated ID position.
 *
 * @param bitmap pointer to the rule ID bitmap structure.
 * @param rules_table_capacity total number of rule IDs available.
 * @param res pointer to store the allocated rule ID.
 *
 * @return FILTER_ERR_OKAY if ID allocated successfully, FILTER_ERR_FULL if no IDs available.
 */
static fw_filter_err_t rules_reserve_id(fw_rule_id_bitmap_t *bitmap, uint16_t rules_table_capacity, uint16_t* res) {
    uint16_t total_ids = rules_table_capacity;
    for (uint16_t i = 0; i < total_ids; i++) {
        uint16_t id_to_check = (bitmap->last_allocated_rule_id + i) % total_ids;

        uint16_t block_idx = id_to_check / BITMAP_BLOCK_SIZE;
        uint8_t bit_pos = id_to_check & MOD64;

        uint64_t mask = 1ULL << bit_pos;

        if (!(bitmap->id_bitmap[block_idx] & mask)) {
            bitmap->id_bitmap[block_idx] |= mask;
            bitmap->last_allocated_rule_id = id_to_check;
            *res = id_to_check;
            return FILTER_ERR_OKAY;
        }
    }
    return FILTER_ERR_FULL;
}
/**
 * Free a previously allocated rule ID by clearing its bit in the bitmap.
 * The default rule (ID 0) cannot be freed and attempts to free it are ignored.
 *
 * @param bitmap pointer to the rule ID bitmap structure.
 * @param id rule ID to free.
 */
static void rules_free_id(fw_rule_id_bitmap_t *bitmap, uint16_t id) {
    if (id == DEFAULT_RULE) {
        return;
    }
    uint16_t block = id / BITMAP_BLOCK_SIZE;
    uint64_t idx = 1ull << (id & MOD64);
    bitmap->id_bitmap[block] &= ~idx;
}

/* PP call parameters for webserver to call filters and update rules */
#define FW_SET_DEFAULT_ACTION 0
#define FW_ADD_RULE 1
#define FW_DEL_RULE 2

typedef enum {
    FILTER_PROTO_UDP,
    FILTER_PROTO_TCP,
    FILTER_PROTO_IMCP
} fw_protocol_id_t;

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

typedef enum {
    FILTER_RET_ERR = 0,
    FILTER_RET_RULE_ID = 1
} fw_ret_args_t;

/**
 * Initialise filter state.
 *
 * @param state address of filter state.
 * @param rules address of rules table.
 * @param rules_capacity capacity of rules table.
 * @param internal_instances address of internal instances.
 * @param external_instances address of external instances.
 * @param instances_capacity capacity of instance tables.
 * @param default_action default action of filter.
 */
static void fw_filter_state_init(fw_filter_state_t *state,
                                 void *rules,
                                 void* rule_id_bitmap,
                                 uint16_t rules_capacity,
                                 void *internal_instances,
                                 void *external_instances,
                                 uint16_t instances_capacity,
                                 fw_action_t default_action)
{
    state->rule_table = (fw_rule_table_t *)rules;
    state->rules_capacity= rules_capacity;
    state->rule_id_bitmap = (fw_rule_id_bitmap_t *) rule_id_bitmap;
    uint16_t res;
    rules_reserve_id(state->rule_id_bitmap, rules_capacity, &res);
    if (FW_DEBUG_OUTPUT && res != 0) {
        sddf_printf("ERROR ON FILTER BIT MAP CREATION\n");
    }
    state->instances_capacity = instances_capacity;
    state->internal_instances_table = (fw_instances_table_t *)internal_instances;
    state->external_instances_table = (fw_instances_table_t *)external_instances;
    state->default_action = default_action;
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
static fw_filter_err_t fw_filter_add_rule(fw_filter_state_t *state,
                                          uint32_t src_ip,
                                          uint16_t src_port,
                                          uint32_t dst_ip,
                                          uint16_t dst_port,
                                          uint8_t src_subnet,
                                          uint8_t dst_subnet,
                                          bool src_port_any,
                                          bool dst_port_any,
                                          fw_action_t action,
                                          uint16_t *rule_id)
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

    if (rules_reserve_id(state->rule_id_bitmap, state->rules_capacity, rule_id) != FILTER_ERR_OKAY) {
        return FILTER_ERR_FULL;
    }
    empty_slot->rule_id = *rule_id;
    state->rule_table->size++; 

    return FILTER_ERR_OKAY;
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
static fw_filter_err_t fw_filter_add_instance(fw_filter_state_t *state,
                                              uint32_t src_ip,
                                              uint16_t src_port,
                                              uint32_t dst_ip,
                                              uint16_t dst_port,
                                              bool default_action,
                                              uint16_t rule_id)
{
    if (state->internal_instances_table->size >= state->instances_capacity) {
        return FILTER_ERR_FULL;
    }
    for (uint16_t i = 0; i < state->internal_instances_table->size; i++) {
        fw_instance_t *instance = state->internal_instances_table->instances + i;

        /* Connection has already been established */
        if (((instance->rule_id == 0 && default_action) || (instance->rule_id == rule_id)) &&
            instance->src_ip == src_ip &&
            instance->src_port == src_port &&
            instance->dst_ip == dst_ip &&
            instance->dst_port == dst_port)
        {
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
static fw_action_t fw_filter_find_action(fw_filter_state_t *state,
                                         uint32_t src_ip,
                                         uint16_t src_port,
                                         uint32_t dst_ip,
                                         uint16_t dst_port,
                                         uint16_t *rule_id)
{
    /* Fist check external instances */
    for (uint16_t i = 0; i < state->external_instances_table->size; i++) {
        fw_instance_t *instance = state->external_instances_table->instances + i;

        if (instance->src_port != dst_port || instance->dst_port != src_port) {
            continue;
        }

        if (instance->src_ip != dst_ip || instance->dst_ip != src_ip) {
            continue;
        }

        *rule_id = instance->rule_id;
        return FILTER_ACT_ESTABLISHED;
    }

    /* Check rules */
    fw_rule_t *match = NULL;
    for (uint16_t i = 0; i < state->rule_table->size; i++) {
        fw_rule_t *rule = state->rule_table->rules + i;

        /* Check port numbers first */
        if ((!rule->src_port_any && HTONS(rule->src_port) != src_port) || (!rule->dst_port_any && HTONS(rule->dst_port) != dst_port)) {
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

    if (match) {
        *rule_id = match->rule_id;
        return match->action;
    }

    return FILTER_ACT_NONE;
}

/**
 * Remove instances associated with a rule. To be used when a rule is
 * deleted or default action is changed.
 *
 * @param state address of filter state.
 * @param default_action whether instances of the default action should be removed.
 * @param rule_id ID of rule that has been deleted.
 *
 * @return error status.
 */
static fw_filter_err_t fw_filter_remove_instances(fw_filter_state_t *state,
                                                  bool default_action,
                                                  uint16_t rule_id)
{
    uint16_t i = 0;
    while (i < state->internal_instances_table->size) {
        fw_instance_t *instance = state->internal_instances_table->instances + i;

        if (default_action && instance->rule_id == DEFAULT_RULE) {
            i++;
            continue;
        }
        if (!default_action && (rule_id != instance->rule_id)) {
            i++;
            continue;
        }

        state->internal_instances_table->instances[i] = state->internal_instances_table->instances[state->internal_instances_table->size - 1];
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
static fw_filter_err_t fw_filter_update_default_action(fw_filter_state_t *state,
                                                       fw_action_t new_action)
{
    fw_action_t old_action = state->default_action;
    if (new_action == old_action) {
        return FILTER_ERR_OKAY;
    }

    if (old_action == FILTER_ACT_CONNECT) {
        fw_filter_err_t err = fw_filter_remove_instances(state, true, 0);
        assert(err == FILTER_ERR_OKAY);
    }

    state->default_action = new_action;

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
static fw_filter_err_t fw_filter_remove_rule(fw_filter_state_t *state,
                                             uint16_t rule_id)
{
    if (rule_id == DEFAULT_RULE) {
        return FILTER_ERR_INVALID_RULE_ID;
    }
    fw_rule_t *rule = NULL;
    for (uint16_t i = 0; i < state->rule_table->size; i++) {
        if (state->rule_table->rules[i].rule_id == rule_id) {
            rule = state->rule_table->rules + i;
            break;
        }
    }

    if (!rule) {
        return FILTER_ERR_INVALID_RULE_ID;
    }

    fw_action_t rule_action = rule->action;
    if (rule_action == FILTER_ACT_CONNECT) {
        fw_filter_err_t err = fw_filter_remove_instances(state, false, rule_id);
        assert(err == FILTER_ERR_OKAY);
    }

    rules_free_id(state->rule_id_bitmap, rule_id);
    generic_array_shift(state->rule_table->rules, 
        sizeof(fw_rule_t), state->rule_table->size, rule - state->rule_table->rules);
    state->rule_table->size--;
    return FILTER_ERR_OKAY;
}
