/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <stdbool.h>
#include <stdint.h>

// TODO:
// - Handle simultaneous closing https://www.rfc-editor.org/rfc/rfc793#section-3.5
// - Figure out a solution to whether local or external instances should be checked first
// - Rectify whether dst_ip/src_ip should be stored in src_ip/dst_ip of instances (which filter's perspective?)
// - Implement timer ticks for removing timed out and closed connections from instances
// - Handle re-opening connections after closure
// - Handle re-using filter data structure with different pointer types
// - TCP instance regions are a different size to the generic, since `fw_tcp_instance_t` and `fw_instance_t` are not
// necessarily the same size. This needs to be reflected in the metaprogram.

/* Data recorded from the last received packet in a TCP connection */
typedef struct fw_tcp_interface_state {
    uint8_t flags; /* flags set in last received instance packet. fin flag is only unset upon final ack */
    uint32_t seq;  /* sequence number of last received instance packet. Once fin is received, seq is only implemented
                      upon final ack */
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
    /* What state it is currently expected to be in currently */
    fw_tcp_conn_state_t current_state;

    fw_tcp_interface_state_t local;
    fw_tcp_interface_state_t external;
    /* Byte numbers expected from both sides */
    uint32_t local_next_seq;
    uint32_t extern_next_seq;
    /* tick of last packet received */
    uint64_t timestamp;
    /* ID of the rule this instance was created from. Allows instances
    to be removed upon rule removal */
    uint16_t rule_id;
} fw_tcp_instance_t;

/* States relative to the filter's instance based on
 * https://www.ibm.com/support/pages/flowchart-tcp-connections-and-their-definition/ */
typedef enum {
    /* no traffic has been seen (listen and closed combined) */
    TCP_NONE,
    /* TCP client has sent its first message in the three-way handshake. This message has the SYN bit set */
    TCP_SYN_SENT,
    /* TCP server has received the first TCP message from the client in the three-way TCP open hand-shake, aka SYN-ACK
       received */
    TCP_SYN_RCVD,
    /* three-way syn handshake has been completed, ACK from original client returned */
    TCP_ESTABLISHED,
    /* local side sent a FIN; waiting for an ACK or a FIN from the remote side (FIN-WAIT-1) */
    TCP_FIN_WAIT_1,
    /* remote side acknowledged our FIN; waiting for the remote side's FIN (FIN-WAIT-2) */
    TCP_FIN_WAIT_2,
    /* remote side sent a FIN and we acknowledged it; waiting for local application to close (CLOSE-WAIT) */
    TCP_CLOSE_WAIT,
    /* local side sent its final FIN after being in CLOSE_WAIT; waiting for final ACK (LAST-ACK) */
    TCP_LAST_ACK,
    /* simultaneous close: both sides sent FINs without receiving ACKs first (CLOSING) */
    TCP_CLOSING,
    /* this connection is closed but the firewall is waiting so stray packets are handled (TIME-WAIT) */
    TCP_TIME_WAIT,
    /* A specific error case for if the transition is invalid and should be dropped */
    TCP_INVALID,
} fw_tcp_conn_state_t;

/* Bits used to store TCP flags */
#define FW_TCP_FIN_BIT (1 << 0)
#define FW_TCP_SYN_BIT (1 << 1)
#define FW_TCP_RST_BIT (1 << 2)
#define FW_TCP_ACK_BIT (1 << 4)

/* Convert TCP flags to a word */
static inline uint8_t fw_tcp_flags_to_bits(bool syn, bool ack, bool fin, bool rst) {
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

/* Check if a network packet matches a tracked connection instance in either direction */
static inline bool fw_tcp_instance_match(const fw_tcp_instance_t *instance, uint32_t src_ip, uint16_t src_port,
                                         uint32_t dst_ip, uint16_t dst_port) {
    bool forward = (instance->src_ip == src_ip && instance->src_port == src_port && instance->dst_ip == dst_ip &&
                    instance->dst_port == dst_port);

    bool reverse = (instance->src_ip == dst_ip && instance->src_port == dst_port && instance->dst_ip == src_ip &&
                    instance->dst_port == src_port);

    return forward || reverse;
}

/* Find firewall action for a given src & dst ip & port. Matches instances first,
followed by the most specific rule. */
static fw_action_t fw_tcp_filter_find_action(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                             uint32_t dst_ip, uint16_t dst_port, uint16_t *rule_id,
                                             fw_tcp_instance_t **instance) {
    /* We give priority to local instances */
    for (uint16_t i = 0; i < state->internal_instances_table->size; i++) {
        fw_tcp_instance_t *curr_instance = (fw_tcp_instance_t *)((uint8_t *)state->internal_instances_table->instances +
                                                                 (i * sizeof(fw_tcp_instance_t)));
        if (!fw_tcp_instance_match(curr_instance, src_ip, src_port, dst_ip, dst_port)) {
            continue;
        }

        *rule_id = curr_instance->rule_id;
        if (instance) {
            *instance = curr_instance;
        }
        return FILTER_ACT_ESTABLISHED;
    }

    /* Then the other filter's instances */
    for (uint16_t i = 0; i < state->external_instances_table->size; i++) {
        fw_tcp_instance_t *curr_instance = (fw_tcp_instance_t *)((uint8_t *)state->external_instances_table->instances +
                                                                 (i * sizeof(fw_tcp_instance_t)));
        if (!fw_tcp_instance_match(curr_instance, src_ip, src_port, dst_ip, dst_port)) {
            continue;
        }

        *rule_id = curr_instance->rule_id;
        if (instance) {
            *instance = curr_instance;
        }
        return FILTER_ACT_ESTABLISHED;
    }

    /* Check rules for best match otherwise we match with the default rule */
    fw_rule_t *match = NULL;
    for (uint16_t i = DEFAULT_ACTION_IDX + 1; i < state->rule_table->size; i++) {
        fw_rule_t *rule = state->rule_table->rules + i;

        /* Check port numbers first */
        if ((!rule->src_port_any && rule->src_port != src_port) ||
            (!rule->dst_port_any && rule->dst_port != dst_port)) {
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

    if (match == NULL) {
        match = &state->rule_table->rules[DEFAULT_ACTION_IDX];
    }

    *rule_id = match->rule_id;
    return (fw_action_t)match->action;
}

/* Valid flags for the TCP final ack sent/closed connection states */
static inline bool fw_tcp_final_ack_sent(uint8_t local_flags, uint8_t extern_flags) {
    return ((local_flags & FW_TCP_FIN_BIT) && (extern_flags & FW_TCP_FIN_BIT) && (local_flags & FW_TCP_ACK_BIT) &&
            (extern_flags & FW_TCP_ACK_BIT));
}

static inline fw_tcp_conn_state_t fw_tcp_next_state(fw_tcp_conn_state_t current, uint8_t flags, bool is_forward) {
    bool syn = (flags & FW_TCP_SYN_BIT);
    bool ack = (flags & FW_TCP_ACK_BIT);
    bool fin = (flags & FW_TCP_FIN_BIT);
    bool rst = (flags & FW_TCP_RST_BIT);

    // Immediate teardown if RST flag is present
    if (rst) {
        return TCP_NONE;
    }

    switch (current) {
    case TCP_NONE:
        // Initiates handshake
        if (syn && !ack && is_forward)
            return TCP_SYN_SENT;
        // TCP_INVALID for out-of-order packets to unallocated sessions
        return TCP_INVALID;
    case TCP_SYN_SENT:
        // Syn ack response
        if (syn && ack && !is_forward)
            return TCP_SYN_RCVD;
        // Simultaneous open
        if (syn && !ack && !is_forward)
            return TCP_SYN_RCVD;
        // Allow local SYN retransmissions
        if (syn && !ack && is_forward)
            return current;
        // unexpected flags or bad sequences during initial handshake are invalid
        return TCP_INVALID;
    case TCP_SYN_RCVD:
        // Final ack in 3 way handshake is sent
        if (ack && !syn && is_forward)
            return TCP_ESTABLISHED;
        // Allow external SYN ACK retransmissions if the final local ACK was dropped
        if (syn && ack && !is_forward)
            return current;
        // Allow SYN retransmissions if executing a simultaneous open
        if (syn && !ack && !is_forward)
            return current;
        return TCP_INVALID;
    case TCP_ESTABLISHED:
        if (fin) {
            // Active close initiated by forward path client
            if (is_forward)
                return TCP_FIN_WAIT_1;
            // Passive close initiated by external path server
            else
                return TCP_CLOSE_WAIT;
        }
        // If invalid syn when connection is already established, invalid packet
        if (syn)
            return TCP_INVALID;
        // Normal packet can pass through
        return current;
    // Active closer sent a FIN, waiting for response
    case TCP_FIN_WAIT_1:
        // Other side acknowledges FIN and sends its own FIN-ACK in normal close
        if (ack && fin && !is_forward)
            return TCP_TIME_WAIT;
        // Simultaneous close, other side sent a FIN, but has not ACKed local sent FIN yet
        if (fin && !ack && !is_forward)
            return TCP_CLOSING;
        // Other side acknowledged FIN in normal close
        if (ack && !is_forward)
            return TCP_FIN_WAIT_2;

        // Local can retransmit fin
        if (fin && !ack && is_forward)
            return current;

        // Allow pure ACK packets to pass through such as data ACKs
        if (ack)
            return current;
        return TCP_INVALID;
    // Other side acknowledged our FIN
    case TCP_FIN_WAIT_2:
        // Received the final FIN from the remote side
        if (fin && !is_forward)
            return TCP_TIME_WAIT;
        if (ack)
            return current;

        return TCP_INVALID;
    case TCP_CLOSING:
        // Remote side ACKed our original FIN
        if (ack && !is_forward)
            return TCP_TIME_WAIT;
        // Other side can retransmit FIN if closing simultaneously
        if (fin && !is_forward)
            return current;
        // Invalid if malformed packet when closing
        return TCP_INVALID;
    // Passive close, external is closing
    case TCP_CLOSE_WAIT:
        // Local is ready to terminate and sends its final FIN packet
        if (fin && is_forward)
            return TCP_LAST_ACK;
        // Allow data ack packets to continue flowing while this side closing decides to close.
        return current;
    // Passive closer sent its final FIN, waiting for ack from remote server
    case TCP_LAST_ACK:
        // Server sends the final ACK back, meaning connection is fully closed
        if (ack && !is_forward)
            return TCP_NONE;
        // Local can retransmit final FIN
        if (fin && is_forward)
            return current;
        return TCP_INVALID;
    case TCP_TIME_WAIT:
        // Lingering state handled exclusively via timer tick sweeps, currently no timer so just a stub
        return current;
    }
    return TCP_INVALID;
}

/* Create a new connection instance generated from a FILTER_ACT_CONNECT rule in a filters
local instances region */
static inline fw_filter_err_t fw_filter_add_instance(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                                     uint32_t dst_ip, uint16_t dst_port, uint16_t rule_id,
                                                     uint32_t seq) {
    fw_tcp_instance_t *internal_array = (fw_tcp_instance_t *)state->internal_instances_table->instances;
    for (uint16_t i = 0; i < state->internal_instances_table->size; i++) {
        fw_tcp_instance_t *instance = &internal_array[i];

        // Cleanup happens here, if any closed instance, remove
        if (instance->current_state == TCP_NONE || instance->current_state == TCP_TIME_WAIT) {
            uint16_t last_idx = state->internal_instances_table->size - 1;

            // Swap the closed entry with the last active entry in the array
            internal_array[i] = internal_array[last_idx];

            // Shrink the tracker size counter
            state->internal_instances_table->size--;

            // Do not increment yet, as need to inspect newly swapped element
            continue;
        }

        /* Check whether connection has already been established */
        if (fw_tcp_instance_match(instance, src_ip, src_port, dst_ip, dst_port)) {
            return FILTER_ERR_DUPLICATE;
        }
    }

    if (state->internal_instances_table->size >= state->instances_capacity) {
        return FILTER_ERR_FULL;
    }

    fw_tcp_instance_t *empty_slot = &internal_array[state->internal_instances_table->size];
    empty_slot->rule_id = rule_id;
    empty_slot->src_ip = src_ip;
    empty_slot->src_port = src_port;
    empty_slot->dst_ip = dst_ip;
    empty_slot->dst_port = dst_port;
    empty_slot->local.flags = FW_TCP_SYN_BIT;
    empty_slot->local.seq = seq;
    empty_slot->external.flags = 0;
    empty_slot->external.seq = 0;
    empty_slot->current_state = TCP_SYN_SENT;
    state->internal_instances_table->size++;
    return FILTER_ERR_OKAY;
}
