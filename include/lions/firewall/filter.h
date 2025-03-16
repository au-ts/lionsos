#pragma once

#include <microkit.h>
#include <sddf/util/util.h>
#include <stdint.h>
#include <stdbool.h>

#define IPV4_ADDR_BIT_LEN 32

#define FIREWALL_NUM_RULES 256
#define FIREWALL_NUM_INSTANCES 512

typedef enum {
    OKAY = 0,   /* No error */
	FULL,  /* Data structure is full */
	DUPLICATE,	/* Duplicate entry exists */
    CLASH, /* Entry clashes with existing entry */
    INVALID_RULE_ID /* Rule id does not point to a valid entry */
} firewall_filter_error_t;

typedef enum {
    NONE,   /* No rule exists */
	ALLOW,  /* Allow traffic */
	DROP,	/* Drop traffic */
    CONNECT, /* Allow traffic matching this rule, and add a rule allowing traffic in the opposite direction */
    ESTABLISHED, /* Traffic has been established in this direction, allow return traffic */
} firewall_action_t;

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
    uint8_t rule_id; /* index of the rule that this is an instance of. Book-keeping for instance creator. */
    uint32_t src_ip; /* ip of connection target */
    uint16_t src_port; /* port of connection target */
    uint32_t dst_ip; /* ip of connection creator */
    uint16_t dst_port; /* port of connection creator */
} firewall_instance_t;

typedef struct firewall_filter_state {
    firewall_rule_t *rules;
    firewall_instance_t *internal_instances; /* Instances for other filter to check */
    firewall_instance_t *external_instances; /* Instances for this filter to check against*/
    firewall_action_t default_action;
} firewall_filter_state_t;

/* PP call parameters for webserver to call filters */
#define FIREWALL_SET_DEFAULT_ACTION 0
#define FIREWALL_ADD_RULE 1
#define FIREWALL_DEL_RULE 2

typedef enum {
    ACTION = 0,
    RULE_ID = 1,
    SRC_IP = 2,
    SRC_PORT = 3,
    DST_IP = 4,
    DST_PORT = 5,
    SRC_SUBNET = 6,
    DST_SUBNET = 7,
    SRC_ANY_PORT = 8,
    DST_ANY_PORT = 9
} firewall_args_t;

typedef enum {
    RET_STATUS = 0,
    RET_RULE_ID = 1
} firewall_return_args_t;

static uint8_t firewall_filter_match_bits(uint32_t ip1, uint32_t ip2, uint8_t mask_bits)
{
    const uint8_t bits_to_check = MIN(IPV4_ADDR_BIT_LEN, mask_bits);
    for (uint8_t bit_no = 0; bit_no < bits_to_check; bit_no++) {
        if ((BIT(bit_no) & ip1) != (BIT(bit_no) & ip2)) {
            return bit_no;
        }
    }
    return bits_to_check;
}

static void firewall_filter_state_init(firewall_filter_state_t *state, 
                                       void *rules, 
                                       void *internal_instances, 
                                       void * external_instances, 
                                       firewall_action_t default_action)
{
    state->rules = (firewall_rule_t *)rules;
    state->internal_instances = (firewall_instance_t *)internal_instances;
    state->external_instances = (firewall_instance_t *)external_instances;
    state->default_action = default_action;
}

static firewall_filter_error_t firewall_filter_add_rule(firewall_filter_state_t *state,
                                    uint32_t src_ip,
                                    uint16_t src_port,
                                    uint32_t dst_ip,
                                    uint16_t dst_port,
                                    uint8_t src_subnet,
                                    uint8_t dst_subnet,
                                    bool src_port_any,
                                    bool dst_port_any,
                                    firewall_action_t action,
                                    uint8_t *rule_id)
{
    firewall_rule_t *empty_slot = NULL;
    for (uint16_t i = 0; i < FIREWALL_NUM_RULES; i++) {
        firewall_rule_t *rule = state->rules + i;

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

        /* One rule applies to a subset of the other's subnet */
        if (src_subnet != rule->src_subnet || dst_subnet != rule->dst_subnet) {
            continue;
        }

        /* Rules apply to different subnets */
        uint8_t src_match = firewall_filter_match_bits(src_ip, rule->src_ip, src_subnet);
        uint8_t dst_match = firewall_filter_match_bits(dst_ip, rule->dst_ip, dst_subnet);
        if (src_match != src_subnet || dst_match != dst_subnet) {
            continue;
        }

        /* There is a clash! */
        if (action == rule->action) {
            return DUPLICATE;
        } else {
            return CLASH;
        }
    }

    if (empty_slot == NULL) {
        return FULL;
    }
    
    empty_slot->src_ip = src_ip;
    empty_slot->src_port = src_port;
    empty_slot->dst_ip = dst_ip;
    empty_slot->dst_port = dst_port;
    empty_slot->src_subnet = src_subnet;
    empty_slot->dst_subnet = dst_subnet;
    empty_slot->src_port_any = src_port_any;
    empty_slot->dst_port_any = dst_port_any;
    empty_slot->action = action;
    *rule_id = empty_slot - state->rules;

    return OKAY;
}

static firewall_filter_error_t firewall_filter_add_instance(firewall_filter_state_t *state,
                                                            uint32_t src_ip,
                                                            uint16_t src_port,
                                                            uint32_t dst_ip,
                                                            uint16_t dst_port,
                                                            uint8_t rule_id)
{
    firewall_instance_t *empty_slot = NULL;
    for (uint16_t i = 0; i < FIREWALL_NUM_INSTANCES; i++) {
        firewall_instance_t *instance = state->internal_instances + i;

        if (!instance->valid) {
            if (empty_slot == NULL) {
                empty_slot = instance;
            }
            continue;
        }

        /* Connection has already been established */
        if (instance->rule_id == rule_id &&
            instance->src_ip == src_ip &&
            instance->src_port == src_port &&
            instance->dst_ip == dst_ip &&
            instance->dst_port == dst_port)
        {
            return DUPLICATE;
        }
    }

    if (empty_slot == NULL) {
        return FULL;
    }

    empty_slot->valid = true;
    empty_slot->rule_id = rule_id;
    empty_slot->src_ip = dst_ip;
    empty_slot->src_port = dst_port;
    empty_slot->dst_ip = src_ip;
    empty_slot->dst_port = src_port;
    return OKAY;
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
    for (uint16_t i = 0; i < FIREWALL_NUM_INSTANCES; i++) {
        firewall_instance_t *instance = state->external_instances + i;

        if (!instance->valid) {
            continue;
        }

        if (instance->src_port != src_port || instance->dst_port != dst_port) {
            continue;
        }

        uint8_t src_match = firewall_filter_match_bits(src_ip, instance->src_ip, IPV4_ADDR_BIT_LEN);
        if (src_match < IPV4_ADDR_BIT_LEN) {
            continue;
        }

        uint8_t dst_match = firewall_filter_match_bits(dst_ip, instance->dst_ip, IPV4_ADDR_BIT_LEN);
        if (dst_match < IPV4_ADDR_BIT_LEN) {
            continue;
        }

        *rule_id = instance->rule_id;
        return ESTABLISHED;
    }

    /* Check rules */
    firewall_rule_t *rule_match = NULL;
    uint8_t max_src_match = 0;
    uint8_t max_dst_match = 0;
    for (uint16_t i = 0; i < FIREWALL_NUM_RULES; i++) {
        firewall_rule_t *rule = state->rules + i;

        if (!rule->valid) {
            continue;
        }

        /* Check port numbers first */
        if ((!rule->src_port_any && rule->src_port != src_port) || (!rule->dst_port_any && rule->dst_port != dst_port)) {
            continue;
        }

        /* Match on src addr first */
        uint8_t src_match = firewall_filter_match_bits(src_ip, rule->src_ip, rule->src_subnet);
        if (src_match < rule->src_subnet || src_match < max_src_match) {
            continue;
        }

        /* Now match on dst addr */
        uint8_t dst_match = firewall_filter_match_bits(dst_ip, rule->dst_ip, rule->dst_subnet);
        if (dst_match < rule->dst_subnet || dst_match < max_dst_match) {
            continue;
        }

        /* This if the first match we've found */
        if (rule_match == NULL) {
            rule_match = rule;
            max_src_match = src_match;
            max_dst_match = dst_match;
        }

        /* Should not be two rules for the same src and dst subnet and port */
        assert(src_match > max_src_match ||
               dst_match > max_dst_match ||
               rule->src_port_any != rule_match->src_port_any ||
               rule->dst_port_any != rule_match->dst_port_any);

        /* We give priority to source matches over destination matches */
        if (src_match > max_src_match ||
            dst_match > max_dst_match || /* source or dst ip match more bits */
            (!rule->src_port_any && rule_match->src_port_any) || /* source port number is a stronger match */
            ((rule->src_port_any == rule_match->src_port_any) && !rule->dst_port_any)) /* destination port number is a stronger match */
        {
            rule_match = rule;
            max_src_match = src_match;
            max_dst_match = dst_match;
        }
    }

    if (rule_match) {
        *rule_id = rule_match - state->rules;
        return rule_match->action;
    }

    return NONE;
}

static firewall_filter_error_t firewall_filter_remove_instances(firewall_filter_state_t *state,
                                                                uint8_t rule_id)
{
    for (uint16_t i = 0; i < FIREWALL_NUM_INSTANCES; i++) {
        firewall_instance_t *instance = state->internal_instances + i;
        if (!instance->valid || instance->rule_id != rule_id) {
            continue;
        }

        instance->valid = false;
    }
    return OKAY;
}

static firewall_filter_error_t firewall_filter_remove_rule(firewall_filter_state_t *state,
    uint8_t rule_id)
{
    firewall_rule_t *rule = state->rules + rule_id;
    if (!rule->valid) {
        return INVALID_RULE_ID;
    }
    
    firewall_filter_error_t err = firewall_filter_remove_instances(state, rule_id);
    assert(err == OKAY);

    rule->valid = false;

    return OKAY;
}
