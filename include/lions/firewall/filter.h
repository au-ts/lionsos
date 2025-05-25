#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <lions/firewall/common.h>

typedef enum {
    FILTER_ERR_OKAY = 0,   /* No error */
	FILTER_ERR_FULL,  /* Data structure is full */
	FILTER_ERR_DUPLICATE,	/* Duplicate entry exists */
    FILTER_ERR_CLASH, /* Entry clashes with existing entry */
    FILTER_ERR_INVALID_RULE_ID /* Rule id does not point to a valid entry */
} firewall_filter_err_t;

static const char *firewall_filter_err_str[] = {
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
} firewall_action_t;

static const char *firewall_filter_action_str[] = {
    "No rule",
    "Allow",
    "Drop",
    "Connect",
    "Established"
};

typedef struct firewall_rule {
    bool valid;
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t dst_ip;
    uint16_t dst_port;
    uint8_t src_subnet; /* ip any is encoded as subnet mask 0 */
    uint8_t dst_subnet;
    bool src_port_any;
    bool dst_port_any;
    firewall_action_t action;
} firewall_rule_t;

/* These entries are to inform the other filter component that a connection
has been established, and that traffic should now be allowed back */
typedef struct firewall_instance {
    bool valid;
    bool default_rule; /* indicates that this is an instance of the default rule */
    uint16_t rule_id; /* index of the rule that this is an instance of. Book-keeping for instance creator. */
    uint32_t src_ip; /* ip of connection target */
    uint16_t src_port; /* port of connection target */
    uint32_t dst_ip; /* ip of connection creator */
    uint16_t dst_port; /* port of connection creator */
} firewall_instance_t;

typedef struct firewall_filter_state {
    firewall_rule_t *rules;
    uint16_t rules_capacity;
    firewall_instance_t *internal_instances; /* Instances for other filter to check */
    firewall_instance_t *external_instances; /* Instances for this filter to check against */
    uint16_t instances_capacity;
    firewall_action_t default_action;
} firewall_filter_state_t;

/* PP call parameters for webserver to call filters */
#define FIREWALL_SET_DEFAULT_ACTION 0
#define FIREWALL_ADD_RULE 1
#define FIREWALL_DEL_RULE 2

typedef enum {
    FILTER_PROTO_UDP,
    FILTER_PROTO_TCP,
    FILTER_PROTO_IMCP
} firewall_protocol_id_t;

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
} firewall_args_t;

typedef enum {
    FILTER_RET_ERR = 0,
    FILTER_RET_RULE_ID = 1
} firewall_ret_args_t;

static void firewall_filter_state_init(firewall_filter_state_t *state, 
                                       void *rules, 
                                       uint16_t rules_capacity,
                                       void *internal_instances, 
                                       void *external_instances, 
                                       uint16_t instances_capacity,
                                       firewall_action_t default_action)
{
    state->rules = (firewall_rule_t *)rules;
    state->rules_capacity = rules_capacity;
    state->internal_instances = (firewall_instance_t *)internal_instances;
    state->external_instances = (firewall_instance_t *)external_instances;
    state->instances_capacity = instances_capacity;
    state->default_action = default_action;
}

static firewall_filter_err_t firewall_filter_add_rule(firewall_filter_state_t *state,
                                    uint32_t src_ip,
                                    uint16_t src_port,
                                    uint32_t dst_ip,
                                    uint16_t dst_port,
                                    uint8_t src_subnet,
                                    uint8_t dst_subnet,
                                    bool src_port_any,
                                    bool dst_port_any,
                                    firewall_action_t action,
                                    uint16_t *rule_id)
{
    firewall_rule_t *empty_slot = NULL;
    for (uint16_t i = 0; i < state->rules_capacity; i++) {
        firewall_rule_t *rule = (firewall_rule_t *)(state->rules + i);

        if (!rule->valid) {
            if (empty_slot == NULL) {
                empty_slot = rule;
            }
            continue;
        }

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
        if ((SUBNET_MASK(src_subnet) & src_ip) != (SUBNET_MASK(rule->src_subnet) & rule->src_ip)) {
            continue;
        }

        /* Rules apply to different destination subnets */
        if ((SUBNET_MASK(dst_subnet) & dst_ip) != (SUBNET_MASK(rule->dst_subnet) & rule->dst_ip)) {
            continue;
        }

        /* There is a clash! */
        if (action == rule->action) {
            return FILTER_ERR_DUPLICATE;
        } else {
            return FILTER_ERR_CLASH;
        }
    }

    if (empty_slot == NULL) {
        return FILTER_ERR_FULL;
    }

    empty_slot->valid = true;
    empty_slot->src_ip = SUBNET_MASK(src_subnet) & src_ip;
    empty_slot->src_port = src_port;
    empty_slot->dst_ip = SUBNET_MASK(dst_subnet) & dst_ip;
    empty_slot->dst_port = dst_port;
    empty_slot->src_subnet = src_subnet;
    empty_slot->dst_subnet = dst_subnet;
    empty_slot->src_port_any = src_port_any;
    empty_slot->dst_port_any = dst_port_any;
    empty_slot->action = action;
    *rule_id = empty_slot - state->rules;

    return FILTER_ERR_OKAY;
}

static firewall_filter_err_t firewall_filter_add_instance(firewall_filter_state_t *state,
                                                            uint32_t src_ip,
                                                            uint16_t src_port,
                                                            uint32_t dst_ip,
                                                            uint16_t dst_port,
                                                            bool default_rule,
                                                            uint8_t rule_id)
{
    firewall_instance_t *empty_slot = NULL;
    for (uint16_t i = 0; i < state->instances_capacity; i++) {
        firewall_instance_t *instance = state->internal_instances + i;

        if (!instance->valid) {
            if (empty_slot == NULL) {
                empty_slot = instance;
            }
            continue;
        }

        /* Connection has already been established */
        if (((instance->default_rule && default_rule) || (instance->rule_id == rule_id)) &&
            instance->src_ip == src_ip &&
            instance->src_port == src_port &&
            instance->dst_ip == dst_ip &&
            instance->dst_port == dst_port)
        {
            return FILTER_ERR_DUPLICATE;
        }
    }

    if (empty_slot == NULL) {
        return FILTER_ERR_FULL;
    }

    empty_slot->valid = true;
    empty_slot->default_rule = default_rule;
    empty_slot->rule_id = rule_id;
    empty_slot->src_ip = dst_ip;
    empty_slot->src_port = dst_port;
    empty_slot->dst_ip = src_ip;
    empty_slot->dst_port = src_port;
    return FILTER_ERR_OKAY;
}

/* Finds firewall action for a given src & dst ip & port. Matches instances first,
followed by the most specific rule. */
static firewall_action_t firewall_filter_find_action(firewall_filter_state_t *state,
                                                     uint32_t src_ip,
                                                     uint16_t src_port,
                                                     uint32_t dst_ip,
                                                     uint16_t dst_port,
                                                     uint8_t *rule_id)
{
    /* We give priority to instances */
    for (uint16_t i = 0; i < state->instances_capacity; i++) {
        firewall_instance_t *instance = state->external_instances + i;

        if (!instance->valid) {
            continue;
        }

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
    firewall_rule_t *match = NULL;
    for (uint16_t i = 0; i < state->rules_capacity; i++) {
        firewall_rule_t *rule = state->rules + i;

        if (!rule->valid) {
            continue;
        }

        /* Check port numbers first */
        if ((!rule->src_port_any && rule->src_port != src_port) || (!rule->dst_port_any && rule->dst_port != dst_port)) {
            continue;
        }

        /* Match on src addr first */
        if ((SUBNET_MASK(rule->src_subnet) & src_ip) != (SUBNET_MASK(rule->src_subnet) & rule->src_ip)) {
            continue;
        }

        /* Match on src addr first */
        if ((SUBNET_MASK(rule->dst_subnet) & dst_ip) != (SUBNET_MASK(rule->dst_subnet) & rule->dst_ip)) {
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
        *rule_id = match - state->rules;
        return match->action;
    }

    return FILTER_ACT_NONE;
}

static firewall_filter_err_t firewall_filter_remove_instances(firewall_filter_state_t *state,
                                                                bool default_rule,
                                                                uint8_t rule_id)
{
    for (uint16_t i = 0; i < state->instances_capacity; i++) {
        firewall_instance_t *instance = state->internal_instances + i;
        if (!instance->valid) {
            continue;
        }
        
        if (default_rule && (default_rule != instance->default_rule)) {
            continue;
        }
        
        if (!default_rule && (rule_id != instance->rule_id)) {
            continue;
        }

        instance->valid = false;
    }
    return FILTER_ERR_OKAY;
}

static firewall_filter_err_t firewall_filter_update_default_action(firewall_filter_state_t *state, firewall_action_t new_action)
{
    firewall_action_t old_action = state->default_action;
    if (old_action == FILTER_ACT_CONNECT) {
        firewall_filter_err_t err = firewall_filter_remove_instances(state, true, 0);
        assert(err == FILTER_ERR_OKAY);
    }
    
    state->default_action = new_action;

    return FILTER_ERR_OKAY;
}

static firewall_filter_err_t firewall_filter_remove_rule(firewall_filter_state_t *state, uint8_t rule_id)
{
    firewall_rule_t *rule = state->rules + rule_id;
    if (rule_id >= state->rules_capacity || !rule->valid) {
        return FILTER_ERR_INVALID_RULE_ID;
    }

    firewall_action_t rule_action = rule->action;
    if (rule_action == FILTER_ACT_CONNECT) {
        firewall_filter_err_t err = firewall_filter_remove_instances(state, false, rule_id);
        assert(err == FILTER_ERR_OKAY);
    }
    
    rule->valid = false;

    return FILTER_ERR_OKAY;
}
