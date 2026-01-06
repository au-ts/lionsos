#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>

/* TCP flag bits */
#define FW_TCP_FIN_BIT (1 << 7)
#define FW_TCP_SYN_BIT (1 << 6)
#define FW_TCP_RST_BIT (1 << 5)
#define FW_TCP_ACK_BIT (1 << 3)

/* Data recorded from the last received packet in a TCP connection */
typedef struct fw_tcp_packet_data {
    uint8_t flags; /* flags */
    uint32_t seq; /* sequence number */
} fw_tcp_packet_data_t;

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
    fw_tcp_packet_data_t internal;
    /* data from last packet received by the other interface's filter */
    fw_tcp_packet_data_t external;
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

/* Create a TCP flag from a set of booleans */
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

/**
 * Find the filter action to be applied for a given source and destination ip
 * and port number. First instances are checked so that return traffic may be
 * permitted. If there is not an existing instance, the most specific matching
 * filter rule is returned.
 *
 * @param state address of filter state.
 * @param src_ip source ip to match.
 * @param src_port source port to match.
 * @param dst_ip destination ip to match.
 * @param dst_port destination port to match.
 * @param rule_id id of matching rule.
 * @param tcp_instance pointer to the matching instance. Unmodified if no match.
 *
 * @return filter action to be applied. None is returned if no match is found.
 */
static inline fw_action_t fw_tcp_filter_find_action(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                                uint32_t dst_ip, uint16_t dst_port, uint16_t *rule_id,
                                                fw_tcp_instance_t **tcp_instance)
{
    /* We give priority to (internal) instances */
    for (uint16_t i = 0; i < state->internal_instances_table->size; i++) {
        fw_tcp_instance_t *instance = (fw_tcp_instance_t *)state->internal_instances_table->instances + i;

        if (instance->src_port != dst_port || instance->dst_port != src_port) {
            continue;
        }

        if (instance->src_ip != dst_ip || instance->dst_ip != src_ip) {
            continue;
        }

        *rule_id = instance->rule_id;
        *tcp_instance = instance;
        return FILTER_ACT_ESTABLISHED;
    }

    /* Then the other filter's instances */
    for (size_t iface = 0; iface < state->num_interfaces; iface++) {
        for (uint16_t i = 0; i < state->external_instances_table[iface]->size; i++) {
            fw_tcp_instance_t *instance = (fw_tcp_instance_t *)state->external_instances_table[iface]->instances + i;

            if (instance->src_port != dst_port || instance->dst_port != src_port) {
                continue;
            }

            if (instance->src_ip != dst_ip || instance->dst_ip != src_ip) {
                continue;
            }

            *rule_id = instance->rule_id;
            *tcp_instance = instance;
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

/* Internal client has sent a syn, external hasn't */
static inline bool fw_tcp_syn_sent(uint8_t internal_flags, uint8_t external_flags)
{
    if (internal_flags == FW_TCP_SYN_BIT && !external_flags) {
        return true;
    }

    return false;
}

/* External client has sent a syn, internal has replied with a syn-ack */
static inline bool fw_tcp_synack_sent(uint8_t internal_flags, uint8_t external_flags)
{
    if (internal_flags == (FW_TCP_SYN_BIT | FW_TCP_ACK_BIT) && external_flags == FW_TCP_SYN_BIT) {
        return true;
    }

    return false;
}

/* Internal client initiated the connection and the connection has been
established. Thus the last packet sent by the internal client does not contain
syn. It is possible the last packet sent by the external client was a syn-ack.

Additionally, no client has sent a fin. */
 static inline bool fw_tcp_established(uint8_t internal_flags, uint8_t external_flags)
{
    if (!(internal_flags & FW_TCP_SYN_BIT) && internal_flags & FW_TCP_ACK_BIT &&
        external_flags & FW_TCP_ACK_BIT && !(internal_flags & FW_TCP_FIN_BIT) &&
        !(external_flags & FW_TCP_FIN_BIT))
    {
        return true;
    }

    return false;
}

/* Internal client has sent a fin and the external client has not */
static inline bool fw_tcp_fin_sent(uint8_t internal_flags, uint8_t external_flags)
{
    if (!(internal_flags & FW_TCP_SYN_BIT) &&
        internal_flags & FW_TCP_FIN_BIT &&
        !(external_flags & FW_TCP_FIN_BIT)) {
        return true;
    }

    return false;
}

/* External client has sent a fin, and internal client has replied with a fin-ack */
static inline bool fw_tcp_finack_sent(uint8_t internal_flags, uint8_t external_flags)
{
    if (internal_flags == (FW_TCP_FIN_BIT | FW_TCP_ACK_BIT) &&
        external_flags & FW_TCP_FIN_BIT &&
        !(external_flags & FW_TCP_SYN_BIT)) {
        return true;
    }

    return false;
}

/* Internal client sent a fin, external replied with a fin-ack, internal sent a
final ack. */
static inline bool fw_tcp_final_ack_sent(uint8_t internal_flags, uint8_t external_flags)
{
    if (internal_flags == FW_TCP_ACK_BIT && external_flags == (FW_TCP_FIN_BIT | FW_TCP_ACK_BIT)) {
        return true;
    }

    return false;
}

/* Map the last recorded flags from the internal and external filter to a TCP
connection state. */
static fw_filter_err_t fw_tcp_extract_state(fw_filter_state_t *filter_state,
                                            fw_tcp_instance_t *instance,
                                            fw_tcp_packet_data_t **internal_tracking,
                                            fw_tcp_packet_data_t **external_tracking,
                                            fw_tcp_conn_state_t *conn_state)
{
    /* No TCP state, new connection */
    if (instance == NULL) {
        *conn_state = TCP_NONE;
        return FILTER_ERR_OKAY;
    }

    /* Check if instance is internal */
    *internal_tracking = NULL;
    if (instance >= (fw_tcp_instance_t *)filter_state->internal_instances_table &&
        instance < (fw_tcp_instance_t *)filter_state->internal_instances_table
                   + filter_state->instances_capacity * sizeof(fw_tcp_instance_t)) {
        *internal_tracking = &instance->internal;
        *external_tracking = &instance->external;
    } else {
        /* Check if it is external */
        for (size_t iface = 0; iface < filter_state->num_interfaces; iface++) {
            if (instance >= (fw_tcp_instance_t *)filter_state->external_instances_table &&
                instance < (fw_tcp_instance_t *)filter_state->external_instances_table
                        + filter_state->instances_capacity * sizeof(fw_tcp_instance_t)) {
                *internal_tracking = &instance->external;
                *external_tracking = &instance->internal;
                break;
            }
        }
    }

    if (*internal_tracking == NULL) {
        return FILTER_ERR_INVALID_INSTANCE;
    }

    uint8_t internal_flags = (*internal_tracking)->flags;
    uint8_t external_flags = (*external_tracking)->flags;

    /* Check for established state first to optimise for established traffic (most common case) */
    if (fw_tcp_established(internal_flags, external_flags) ||
        fw_tcp_established(external_flags, internal_flags)) {
        *conn_state = TCP_ESTABLISHED;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_syn_sent(internal_flags, external_flags)) {
        *conn_state = TCP_SYN_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_syn_sent(external_flags, internal_flags)) {
        *conn_state = TCP_SYN_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_synack_sent(internal_flags, external_flags)) {
        *conn_state = TCP_SYNACK_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_synack_sent(external_flags, internal_flags)) {
        *conn_state = TCP_SYNACK_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_fin_sent(internal_flags, external_flags)) {
        *conn_state = TCP_FIN_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_fin_sent(external_flags, internal_flags)) {
        *conn_state = TCP_FIN_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_finack_sent(internal_flags, external_flags)) {
        *conn_state = TCP_FINACK_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_finack_sent(external_flags, internal_flags)) {
        *conn_state = TCP_FINACK_SEEN;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_final_ack_sent(internal_flags, external_flags)) {
        *conn_state = TCP_FINAL_ACK_SENT;
        return FILTER_ERR_OKAY;
    } else if (fw_tcp_final_ack_sent(external_flags, internal_flags)) {
        *conn_state = TCP_CLOSED;
        return FILTER_ERR_OKAY;
    }

    return FILTER_ERR_INVALID_INSTANCE_STATE;
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
 * @param seq sequence number of TCP syn packet received.
 * @param return_instance newly created internal instance.
 *
 * @return error status.
 */
static inline fw_filter_err_t fw_tcp_filter_add_instance(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                                     uint32_t dst_ip, uint16_t dst_port, uint16_t rule_id, uint32_t seq,
                                                     fw_tcp_instance_t **return_instance)
{
    if (state->internal_instances_table->size >= state->instances_capacity) {
        return FILTER_ERR_FULL;
    }

    for (uint16_t i = 0; i < state->internal_instances_table->size; i++) {
        fw_tcp_instance_t *instance = (fw_tcp_instance_t *)state->internal_instances_table->instances + i;

        /* Connection has already been established */
        if (instance->rule_id == rule_id && instance->src_ip == src_ip && instance->src_port == src_port
            && instance->dst_ip == dst_ip && instance->dst_port == dst_port) {
            return FILTER_ERR_DUPLICATE;
        }
    }

    fw_tcp_instance_t *empty_slot = (fw_tcp_instance_t *)state->internal_instances_table->instances +
        state->internal_instances_table->size;
    empty_slot->rule_id = rule_id;
    empty_slot->src_ip = dst_ip;
    empty_slot->src_port = dst_port;
    empty_slot->dst_ip = src_ip;
    empty_slot->dst_port = src_port;
    empty_slot->internal.flags = FW_TCP_SYN_BIT;
    empty_slot->internal.seq = seq;
    empty_slot->external.flags = 0;
    empty_slot->external.seq = 0;
    *return_instance = empty_slot;
    return FILTER_ERR_OKAY;
}

// TODO:
// - Handle simultaneous closing
//   https://www.rfc-editor.org/rfc/rfc793#section-3.5
// - Figure out a solution to whether internal or external instances should be
//   checked first
// - Rectify whether dst_ip/src_ip should be stored in src_ip/dst_ip of
//   instances (which filter's perspective?)
// - Implement timer ticks for removing timed out and closed connections from
//   instances
// - Handle re-opening connections after closure
// - Handle re-using generic filter data structures with different pointer types
// - TCP instance regions are a different size to the generic, since
//   `fw_tcp_instance_t` and `fw_instance_t` are not necessarily the same size.
//   This needs to be reflected in the metaprogram.
// - Extend to handle more than 2 interfaces properly
