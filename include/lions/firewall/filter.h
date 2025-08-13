#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/array_functions.h>

#define MOD64 0x3F
#define BITMAP_BLOCK_SIZE 64

typedef enum {
    FILTER_ERR_OKAY = 0,   /* No error */
	FILTER_ERR_FULL,  /* Data structure is full */
	FILTER_ERR_DUPLICATE,	/* Duplicate entry exists */
    FILTER_ERR_CLASH, /* Entry clashes with existing entry */
    FILTER_ERR_INVALID_RULE_ID /* Rule id does not point to a valid entry */
} fw_filter_err_t;

static const char *fw_filter_err_str[] = {
    "Ok.",
    "Out of memory error.",
    "Duplicate entry.",
    "Clashing entry.",
    "Invalid rule ID."
};

typedef enum {
    FILTER_ACT_NONE,   /* No rule exists */
	FILTER_ACT_ALLOW,  /* Allow traffic */
	FILTER_ACT_DROP,	/* Drop traffic */
    FILTER_ACT_CONNECT, /* Allow traffic matching this rule, and add a rule allowing traffic in the opposite direction */
    FILTER_ACT_ESTABLISHED, /* Traffic has been established in this direction, allow return traffic */
} fw_action_t;

static const char *fw_filter_action_str[] = {
    "No rule",
    "Allow",
    "Drop",
    "Connect",
    "Established"
};

typedef struct fw_rule {
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t dst_ip;
    uint16_t dst_port;
    uint8_t src_subnet; /* ip any is encoded as subnet mask 0 */
    uint8_t dst_subnet;
    bool src_port_any;
    bool dst_port_any;
    fw_action_t action;
    uint16_t rule_id;
} fw_rule_t;

/* These entries are to inform the other filter component that a connection
has been established, and that traffic should now be allowed back */
typedef struct fw_instance {
    uint16_t rule_id; /* index of the rule that this is an instance of. Book-keeping for instance creator. */
    uint32_t src_ip; /* ip of connection target */
    uint16_t src_port; /* port of connection target */
    uint32_t dst_ip; /* ip of connection creator */
    uint16_t dst_port; /* port of connection creator */
} fw_instance_t;

typedef struct fw_instances_table {
    uint16_t capacity;
    uint16_t size;
    fw_instance_t instances[];
} fw_instances_table_t;

typedef struct fw_rule_table {
    uint16_t capacity;
    uint16_t size;
    fw_rule_t rules[];
} fw_rule_table_t;

typedef struct fw_rule_id_bitmap {
    uint16_t last_allocated_rule_id;
    uint64_t id_bitmap[];  
} fw_rule_id_bitmap_t;

/* Functions for the bitmap*/
/* Finds the first rule that is not in use, and marks it as used */
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

/* Sets the bit for the inputted id rule to 0, will never free the default rule id (0) */
static void rules_free_id(fw_rule_id_bitmap_t *bitmap, uint16_t id) {
    if (id == 0) {
        return;
    }
    uint16_t block = id / BITMAP_BLOCK_SIZE;
    uint64_t idx = 1ull << (id & MOD64);
    bitmap->id_bitmap[block] &= ~idx;
}

typedef struct fw_filter_state {
    fw_rule_table_t *rule_table; /* Container for firewall rules */
    fw_rule_id_bitmap_t *rule_id_bitmap; /* Functions for interacting with it assumes 0*/

    fw_instances_table_t *internal_instances_container /* Instances for other filter to check */;
    fw_instances_table_t *external_instances_container; /* Instances for this filter to check against */

    fw_action_t default_action;
} fw_filter_state_t;

/* PP call parameters for webserver to call filters */
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

static void fw_filter_state_init(fw_filter_state_t *state, 
                                 void *rules, 
                                 void* rules_id_bitmap,
                                 uint16_t rules_capacity,
                                 void *internal_instances, 
                                 void *external_instances, 
                                 uint16_t instances_capacity,
                                 fw_action_t default_action)
{
    state->rule_table = (fw_rule_table_t *)rules;
    state->rule_table->capacity = rules_capacity;

    state->rule_id_bitmap = (fw_rule_id_bitmap_t *)rules_id_bitmap;
    uint16_t res;
    rules_reserve_id(state->rule_id_bitmap, rules_capacity, &res);
    if (FW_DEBUG_OUTPUT && res != 0) {
        sddf_printf("ERROR ON FILTER BIT MAP CREATION\n");
    }

    state->internal_instances_container = (fw_instances_table_t *)internal_instances;
    state->internal_instances_container->capacity = instances_capacity;

    state->external_instances_container = (fw_instances_table_t *)external_instances;

    state->default_action = default_action;
}

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
    for (uint16_t i = 0; i < state->rule_table->size ; i++) {
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

    if (state->rule_table->size >= state->rule_table->capacity) {
        return FILTER_ERR_FULL;
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
    
    if (rules_reserve_id(state->rule_id_bitmap, state->rule_table->capacity, rule_id) != FILTER_ERR_OKAY) {
        return FILTER_ERR_FULL;
    }
    empty_slot->rule_id = *rule_id;
    state->rule_table->size++; 

    return FILTER_ERR_OKAY;
}

static fw_filter_err_t fw_filter_add_instance(fw_filter_state_t *state,
                                              uint32_t src_ip,
                                              uint16_t src_port,
                                              uint32_t dst_ip,
                                              uint16_t dst_port,
                                              bool default_rule,
                                              uint16_t rule_id)
{
    if (state->internal_instances_container->size >= state->internal_instances_container->capacity) {
        return FILTER_ERR_FULL;
    }
    for (uint16_t i = 0; i < state->internal_instances_container->size; i++) {
        fw_instance_t *instance = state->internal_instances_container->instances + i;

        /* Connection has already been established */
        if (((instance->rule_id == 0 && default_rule) || (instance->rule_id == rule_id)) &&
            instance->src_ip == dst_ip &&
            instance->src_port == dst_port &&
            instance->dst_ip == src_ip &&
            instance->dst_port == src_port)
        {
            return FILTER_ERR_DUPLICATE;
        }
    }

    fw_instance_t *empty_slot = state->internal_instances_container->instances + state->internal_instances_container->size;

    empty_slot->rule_id = rule_id;
    empty_slot->src_ip = dst_ip;
    empty_slot->src_port = dst_port;
    empty_slot->dst_ip = src_ip;
    empty_slot->dst_port = src_port;
    state->internal_instances_container->size++;
    return FILTER_ERR_OKAY;
}

/* Finds firewall action for a given src & dst ip & port. Matches instances first,
followed by the most specific rule. */
static fw_action_t fw_filter_find_action(fw_filter_state_t *state,
                                         uint32_t src_ip,
                                         uint16_t src_port,
                                         uint32_t dst_ip,
                                         uint16_t dst_port,
                                         uint16_t *rule_id)
{
    /* We give priority to instances */
    for (uint16_t i = 0; i < state->external_instances_container->size; i++) {
        fw_instance_t *instance = state->external_instances_container->instances + i;

        if (instance->src_port != src_port || instance->dst_port != dst_port) {
            continue;
        }

        if (instance->src_ip != src_ip || instance->dst_ip != dst_ip) {
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
        if ((!rule->src_port_any && rule->src_port != src_port) || (!rule->dst_port_any && rule->dst_port != dst_port)) {
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

static fw_filter_err_t fw_filter_remove_instances(fw_filter_state_t *state,
                                                  bool default_rule,
                                                  uint16_t rule_id)
{
    for (uint16_t i = 0; i < state->internal_instances_container->size; i++) {
        fw_instance_t *instance = state->internal_instances_container->instances + i;
        
        if (default_rule && instance->rule_id == 0) {
            continue;
        }
        
        if (!default_rule && (rule_id != instance->rule_id)) {
            continue;
        }
        state->internal_instances_container->instances[i--] = state->internal_instances_container->instances[state->internal_instances_container->size - 1];
        state->internal_instances_container->size--;
    }
    return FILTER_ERR_OKAY;
}

static fw_filter_err_t fw_filter_update_default_action(fw_filter_state_t *state, fw_action_t new_action)
{
    fw_action_t old_action = state->default_action;
    if (old_action == FILTER_ACT_CONNECT) {
        fw_filter_err_t err = fw_filter_remove_instances(state, true, 0);
        assert(err == FILTER_ERR_OKAY);
    }
    
    state->default_action = new_action;

    return FILTER_ERR_OKAY;
}

static fw_filter_err_t fw_filter_remove_rule(fw_filter_state_t *state, uint16_t rule_id)
{
    if (rule_id == 0) {
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

