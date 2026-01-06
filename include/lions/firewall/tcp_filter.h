#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>

// TODO:
// - Handle simultaneous closing https://www.rfc-editor.org/rfc/rfc793#section-3.5
// - Figure out a solution to whether local or external instances should be checked first
// - Rectify whether dst_ip/src_ip should be stored in src_ip/dst_ip of instances (which filter's perspective?)
// - Implement timer ticks for removing timed out and closed connections from instances
// - Handle re-opening connections after closure
// - Handle re-using filter data structure with different pointer types
// - TCP instance regions are a different size to the generic, since `fw_tcp_instance_t` and `fw_instance_t` are not necessarily the same size. This needs to be reflected in the metaprogram.

/* Data recorded from the last received packet in a TCP connection */
typedef struct fw_tcp_interface_state {
    uint8_t flags; /* flags set in last received instance packet. fin flag is only unset upon final ack */
    uint32_t seq; /* sequence number of last received instance packet. Once fin is received, seq is only implemented upon final ack */
} fw_tcp_interface_state_t;

/* TCP filter specific instance */
typedef struct fw_tcp_instance {
    /* source ip of traffic */
    uint32_t src_ip;
    /* destination ip of traffic */
    uint32_t dst_ip;
    /* source port of traffic */
    uint16_t src_port;
    /* destination port of traffic */
    uint16_t dst_port;
    /* data from last packet received by this filter */
    fw_tcp_interface_state_t local;
    /* data from last packet received by neighbouring filter */
    fw_tcp_interface_state_t external;
    /* tick of last packet received */
    uint64_t timestamp;
    /* ID of the rule this instance was created from. Allows instances
    to be removed upon rule removal */
    uint16_t rule_id;
} fw_tcp_instance_t;

typedef enum {
    /* no traffic has been seen */
    TCP_NONE,
    /* this filter has received a syn */
    TCP_SYN_SENT,
    /* neighbour filter has received a syn */
    TCP_SYN_SEEN,
    /* this filter has received a syn-ack */
    TCP_SYNACK_SENT,
    /* neighbour filter has received a syn-ack */
    TCP_SYNACK_SEEN,
    /* three-way syn handshake has been completed */
    TCP_ESTABLISHED,
    /* this filter has received a fin */
    TCP_FIN_SENT,
    /* neighbour filter has received a fin */
    TCP_FIN_SEEN,
    /* this filter has received a fin-ack */
    TCP_FINACK_SENT,
    /* neighbour filter has received a fin-ack */
    TCP_FINACK_SEEN,
    /* this filter has received final ack. Three-way fin handshake has been
    completed */
    TCP_FINAL_ACK_SENT,
    /* neighbour filter has received final ack. Three-way fin handshake has been
    completed */
    TCP_CLOSED
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
static fw_action_t fw_tcp_filter_find_action(fw_filter_state_t *state,
                                             uint32_t src_ip,
                                             uint16_t src_port,
                                             uint32_t dst_ip,
                                             uint16_t dst_port,
                                             uint16_t *rule_id,
                                             fw_tcp_instance_t **instance)
{
    /* We give priority to (local) instances */
    for (uint16_t i = 0; i < state->local_instances_table->size; i++) {
        fw_tcp_instance_t *instance = (fw_tcp_instance_t *)state->local_instances_table->instances + i;

        if (instance->src_port != dst_port || instance->dst_port != src_port) {
            continue;
        }

        if (instance->src_ip != dst_ip || instance->dst_ip != src_ip) {
            continue;
        }

        *rule_id = instance->rule_id;
        return FILTER_ACT_ESTABLISHED;
    }

    /* Then the other filter's instances */
    for (uint16_t i = 0; i < state->extern_instances_table->size; i++) {
        fw_tcp_instance_t *instance = (fw_tcp_instance_t *)state->extern_instances_table->instances + i;

        if (instance->src_port != dst_port || instance->dst_port != src_port) {
            continue;
        }

        if (instance->src_ip != dst_ip || instance->dst_ip != src_ip) {
            continue;
        }

        *rule_id = instance->rule_id;
        return FILTER_ACT_ESTABLISHED;
    }

    /* Check rules for best match otherwise we match with the default rule */
    fw_rule_t *match = &state->rule_table->rules[DEFAULT_ACTION_IDX];
    for (uint16_t i = DEFAULT_ACTION_IDX + 1; i < state->rule_table->size; i++) {
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

    *rule_id = match->rule_id;
    return (fw_action_t)match->action;
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
    if (instance >= (fw_tcp_instance_t *)filter_state->local_instances_table &&
        instance < (fw_tcp_instance_t *)filter_state->local_instances_table
                   + filter_state->instances_capacity * sizeof(fw_tcp_instance_t)) {
        *local_state = &instance->local;
        *extern_state = &instance->external;
    } else if (instance >= (fw_tcp_instance_t *)filter_state->extern_instances_table &&
        instance < (fw_tcp_instance_t *)filter_state->extern_instances_table
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

/* Create a new connection instance generated from a FILTER_ACT_CONNECT rule in a filters
local instances region */
static fw_filter_err_t fw_tcp_filter_add_instance(fw_filter_state_t *state,
                                                  uint32_t src_ip,
                                                  uint16_t src_port,
                                                  uint32_t dst_ip,
                                                  uint16_t dst_port,
                                                  uint16_t rule_id,
                                                  uint32_t seq,
                                                  fw_tcp_instance_t **return_instance)
{
    if (state->local_instances_table->size >= state->instances_capacity) {
        return FILTER_ERR_FULL;
    }

    for (uint16_t i = 0; i < state->local_instances_table->size; i++) {
        fw_tcp_instance_t *instance = (fw_tcp_instance_t *)state->local_instances_table->instances + i;

        /* Connection has already been established */
        if (instance->rule_id == rule_id &&
            instance->src_ip == src_ip &&
            instance->src_port == src_port &&
            instance->dst_ip == dst_ip &&
            instance->dst_port == dst_port)
        {
            return FILTER_ERR_DUPLICATE;
        }
    }

    fw_tcp_instance_t *empty_slot = (fw_tcp_instance_t *)state->local_instances_table->instances +
        state->local_instances_table->size;
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
