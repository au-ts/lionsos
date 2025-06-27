#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>

// TODO:
// Handle TCP reset
// Handle simultaneous closing https://www.rfc-editor.org/rfc/rfc793#section-3.5
// Figure out a solution to what order instances should be checked
// Implement timer ticks for removing timed out and closed entries
// Handle re-opening connections after closure
// Handle re-using filter data structurew with different pointer types

/* Recorded data from last received instance packet */
typedef struct fw_tcp_interface_state {
    uint8_t flags; /* flags set in last received instance packet. fin flag is only unset upon final ack */
    uint32_t seq; /* sequence number of last received instance packet. Once fin is received, seq is only implemented upon final ack */
} fw_tcp_interface_state_t;

/* Data structure to track connections generated from FILTER_ACT_CONNECT action.
Instances reside in shared memory between filters and are updated as traffic is received */
typedef struct fw_tcp_instance {
    bool default_rule; /* indicates that this is an instance of the default rule */
    uint16_t rule_id; /* index of the rule that this is an instance of. Book-keeping for instance creator. */
    uint32_t src_ip; /* ip of connection target */
    uint16_t src_port; /* port of connection target */
    uint32_t dst_ip; /* ip of connection creator */
    uint16_t dst_port; /* port of connection creator */
    fw_tcp_interface_state_t local; /* data from last packet received by filter whose instance region this entry belongs to */
    fw_tcp_interface_state_t external; /* data from last packet received by neighbouring filter */
    uint64_t timestamp; /* tick of last packet received */
} fw_tcp_instance_t;

typedef enum {
    TCP_NONE, /* No traffic has been scene */
    TCP_SYN_SENT, /* Filter has received a syn */
    TCP_SYN_SEEN, /* Neighbour filter has received a syn */
    TCP_SYNACK_SENT, /* Filter has received a syn-ack */
    TCP_SYNACK_SEEN, /* Neighbour filter has received a syn-ack */
    TCP_ESTABLISHED, /* Three-way syn handshake has been completed */
    TCP_FIN_SENT, /* Filter has received a fin */
    TCP_FIN_SEEN, /* Neighbour filter has received a fin */
    TCP_FINACK_SENT, /* Filter has received a fin-ack */
    TCP_FINACK_SEEN, /* Neighbour filter has received a fin-ack */
    TCP_FINAL_ACK_SENT, /* Filter has received final ack. Three-way fin handshake has been completed */
    TCP_CLOSED /* Neighbour filter has received final ack. Three-way fin handshake has been completed */
} fw_tcp_conn_state_t;

/* Bits used to store TCP flags */
#define FW_TCP_FIN_BIT (1 << 7)
#define FW_TCP_SYN_BIT (1 << 6)
#define FW_TCP_RST_BIT (1 << 5)
#define FW_TCP_ACK_BIT (1 << 3)

/* Convert TCP flags to a word */
static inline uint8_t fw_tcp_flags_to_bits(bool syn, bool ack, bool fin, bool rst)
{
    uint8_t result = 0;
    if (syn) {
        result |= FW_TCP_SYN_BIT;
    }

    if (ack) {
        result |= FW_TCP_ACK_BIT;
    }

    if (fin) {
        result |= FW_TCP_FIN_BIT;
    }

    if (rst) {
        result |= FW_TCP_RST_BIT;
    }

    return result;
}

/* Find firewall action for a given src & dst ip & port. Matches instances first,
followed by the most specific rule. */
static fw_action_t fw_tcp_filter_find_action(fw_filter_state_t *filter_state,
                                             uint32_t src_ip,
                                             uint16_t src_port,
                                             uint32_t dst_ip,
                                             uint16_t dst_port,
                                             uint16_t *rule_id,
                                             fw_tcp_instance_t **instance)
{
    /* We give priority to local instances */
    for (uint16_t i = 0; i < filter_state->instances_capacity; i++) {
        fw_tcp_instance_t *local_instance = (fw_tcp_instance_t *)filter_state->local_instances + i;
        if (local_instance->rule_id == filter_state->rules_capacity) {
            continue;
        }

        if (local_instance->src_port != dst_port ||
            local_instance->dst_port != src_port) {
            continue;
        }

        if (local_instance->src_ip != dst_ip ||
            local_instance->dst_ip != src_ip) {
            continue;
        }

        *rule_id = local_instance->rule_id;
        *instance = local_instance;
        return FILTER_ACT_ESTABLISHED;
    }

    /* We give priority to instances */
    for (uint16_t i = 0; i < filter_state->instances_capacity; i++) {
        fw_tcp_instance_t *external_instance = (fw_tcp_instance_t *)filter_state->extern_instances + i;

        if (external_instance->rule_id == filter_state->rules_capacity) {
            continue;
        }

        if (external_instance->src_port != src_port ||
            external_instance->dst_port != dst_port) {
            continue;
        }

        if (external_instance->src_ip != src_ip ||
            external_instance->dst_ip != dst_ip) {
            continue;
        }

        *rule_id = external_instance->rule_id;
        *instance = external_instance;
        return FILTER_ACT_ESTABLISHED;
    }

    /* Check rules */
    fw_rule_t *match = NULL;
    for (uint16_t i = 0; i < filter_state->rules_capacity; i++) {
        fw_rule_t *rule = filter_state->rules + i;

        if (!rule->valid) {
            continue;
        }

        /* Check port numbers first */
        if ((!rule->src_port_any && rule->src_port != src_port) ||
            (!rule->dst_port_any && rule->dst_port != dst_port)) {
            continue;
        }

        /* Match on src addr first */
        if ((subnet_mask(rule->src_subnet) & src_ip) !=
            (subnet_mask(rule->src_subnet) & rule->src_ip)) {
            continue;
        }

        /* Match on src addr first */
        if ((subnet_mask(rule->dst_subnet) & dst_ip) !=
            (subnet_mask(rule->dst_subnet) & rule->dst_ip)) {
            continue;
        }

        /* This if the first match we've found */
        if (match == NULL) {
            match = rule;
            continue;
        }

        /* We give priority to source matches over destination matches */
        if (rule->src_subnet == match->src_subnet) {
            if (rule->dst_subnet == match->dst_subnet) {
                if (rule->src_port_any == match->src_port_any) {
                    if (!rule->dst_port_any && match->dst_port_any) {
                        /* destination port number is a stronger match */
                        match = rule;
                    }
                } else if (!rule->src_port_any && match->src_port_any) {
                    /* source port number is a stronger match */
                    match = rule;
                }
            } else if (rule->dst_subnet > match->dst_subnet) {
                /* destination subnet is a longer match */
                match = rule;
            }
        } else if (rule->src_subnet > match->src_subnet) {
            /* source subnet is a longer match */
            match = rule;
        }
    }

    if (match) {
        *rule_id = match - filter_state->rules;
        return match->action;
    }

    return FILTER_ACT_NONE;
}

/* Valid flags for the TCP established connection state */
static inline bool fw_tcp_established(uint8_t local_flags, uint8_t extern_flags)
{
    if (!(local_flags & FW_TCP_SYN_BIT) &&
        !(local_flags & FW_TCP_FIN_BIT) &&
        local_flags & FW_TCP_ACK_BIT &&
        !(extern_flags & FW_TCP_FIN_BIT) &&
        extern_flags & FW_TCP_ACK_BIT)
    {
        return true;
    }

    return false;
}

/* Valid flags for the TCP syn sent/seen connection states */
static inline bool fw_tcp_syn_sent(uint8_t local_flags, uint8_t extern_flags)
{
    if (local_flags == FW_TCP_SYN_BIT && extern_flags == 0) {
        return true;
    }

    return false;
}

/* Valid flags for the TCP syn-ack sent/seen connection states */
static inline bool fw_tcp_synack_sent(uint8_t local_flags, uint8_t extern_flags)
{
    if (local_flags == (FW_TCP_SYN_BIT | FW_TCP_ACK_BIT) && extern_flags == FW_TCP_SYN_BIT) {
        return true;
    }

    return false;
}

/* Valid flags for the TCP fin sent/seen connection states */
static inline bool fw_tcp_fin_sent(uint8_t local_flags, uint8_t extern_flags)
{
    if (!(local_flags & FW_TCP_SYN_BIT) &&
        local_flags & FW_TCP_FIN_BIT &&
        !(extern_flags & FW_TCP_FIN_BIT)) {
        return true;
    }

    return false;
}

/* Valid flags for the TCP fin-ack sent/seen connection states */
static inline bool fw_tcp_finack_sent(uint8_t local_flags, uint8_t extern_flags)
{
    if (local_flags == (FW_TCP_FIN_BIT | FW_TCP_ACK_BIT) &&
        extern_flags & FW_TCP_FIN_BIT &&
        !(extern_flags & FW_TCP_SYN_BIT)) {
        return true;
    }

    return false;
}

/* Valid flags for the TCP final ack sent/closed connection states */
static inline bool fw_tcp_finaL_ack_sent(uint8_t local_flags, uint8_t extern_flags)
{
    if (local_flags == FW_TCP_ACK_BIT && extern_flags == (FW_TCP_FIN_BIT | FW_TCP_ACK_BIT)) {
        return true;
    }

    return false;
}

/* Extract TCP connection state from last seen flags contained in the instance */
static fw_filter_err_t fw_tcp_extract_state(fw_filter_state_t *filter_state,
                                            fw_tcp_instance_t *instance,
                                            fw_tcp_interface_state_t **local_state,
                                            fw_tcp_interface_state_t **extern_state,
                                            fw_tcp_conn_state_t *conn_state)
{
    /* No TCP state, new connection */
    if (instance == NULL) {
        *conn_state = TCP_NONE;
        return FILTER_ERR_OKAY;
    }

    /* Check if instance is local or external */
    if (instance >= (fw_tcp_instance_t *)filter_state->local_instances &&
        instance < (fw_tcp_instance_t *)filter_state->local_instances
                   + filter_state->instances_capacity * sizeof(fw_tcp_instance_t)) {
        *local_state = &instance->local;
        *extern_state = &instance->external;
    } else if (instance >= (fw_tcp_instance_t *)filter_state->extern_instances &&
        instance < (fw_tcp_instance_t *)filter_state->extern_instances
                   + filter_state->instances_capacity * sizeof(fw_tcp_instance_t)) {
        *local_state = &instance->external;
        *extern_state = &instance->local;
    } else {
        return FILTER_ERR_INVALID_INSTANCE;
    }

    uint8_t local_flags = (*local_state)->flags;
    uint8_t extern_flags = (*extern_state)->flags;

    /* Check for established state first to optimise for established traffic (most common case) */
    if (fw_tcp_established(local_flags, extern_flags) ||
        fw_tcp_established(extern_flags, local_flags)) {
        *conn_state = TCP_ESTABLISHED;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_syn_sent(local_flags, extern_flags)) {
        *conn_state = TCP_SYN_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_syn_sent(extern_flags, local_flags)) {
        *conn_state = TCP_SYN_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_synack_sent(local_flags, extern_flags)) {
        *conn_state = TCP_SYNACK_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_synack_sent(extern_flags, local_flags)) {
        *conn_state = TCP_SYNACK_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_fin_sent(local_flags, extern_flags)) {
        *conn_state = TCP_FIN_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_fin_sent(extern_flags, local_flags)) {
        *conn_state = TCP_FIN_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_finack_sent(local_flags, extern_flags)) {
        *conn_state = TCP_FINACK_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_finack_sent(extern_flags, local_flags)) {
        *conn_state = TCP_FINACK_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_finaL_ack_sent(local_flags, extern_flags)) {
        *conn_state = TCP_FINAL_ACK_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_finaL_ack_sent(extern_flags, local_flags)) {
        *conn_state = TCP_CLOSED;
        return FILTER_ERR_OKAY;
    }

    return FILTER_ERR_INVALID_INSTANCE_STATE;
}

/* Create a new connecion instance generated from a FILTER_ACT_CONNECT rule in a filters
local instances region */
static fw_filter_err_t fw_tcp_filter_add_instance(fw_filter_state_t *filter_state,
                                                  uint32_t src_ip,
                                                  uint16_t src_port,
                                                  uint32_t dst_ip,
                                                  uint16_t dst_port,
                                                  bool default_rule,
                                                  uint16_t rule_id,
                                                  uint32_t seq,
                                                  fw_tcp_instance_t **return_instance)
{
    fw_tcp_instance_t *empty_slot = NULL;
    for (uint16_t i = 0; i < filter_state->instances_capacity; i++) {
        fw_tcp_instance_t *instance = (fw_tcp_instance_t *)filter_state->local_instances + i;

        if (instance->rule_id == filter_state->rules_capacity) {
            if (empty_slot == NULL) {
                empty_slot = instance;
            }
            continue;
        }

        /* Connection has already been established */
        if (((instance->default_rule && default_rule) || (instance->rule_id == rule_id)) &&
            instance->src_ip == dst_ip &&
            instance->src_port == dst_port &&
            instance->dst_ip == src_ip &&
            instance->dst_port == src_port)
        {
            return FILTER_ERR_DUPLICATE;
        }
    }

    if (empty_slot == NULL) {
        return FILTER_ERR_FULL;
    }

    empty_slot->default_rule = default_rule;
    empty_slot->rule_id = rule_id;
    empty_slot->src_ip = dst_ip;
    empty_slot->src_port = dst_port;
    empty_slot->dst_ip = src_ip;
    empty_slot->dst_port = src_port;
    empty_slot->local.flags = FW_TCP_SYN_BIT;
    empty_slot->local.seq = seq;
    empty_slot->external.flags = 0;
    empty_slot->external.seq = 0;
    *return_instance = empty_slot;
    return FILTER_ERR_OKAY;
}
