/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <os/sddf.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/util.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>

/* Each TCP filter owns an instance table of the connections it created. Each
instance tracks both sides of a connection, and a filter only writes the side
whose packets it receives. Only the owning filter adds and removes instances. */

/* TCP flag bits */
#define FW_TCP_FIN_BIT (1 << 7)
#define FW_TCP_SYN_BIT (1 << 6)
#define FW_TCP_RST_BIT (1 << 5)
#define FW_TCP_ACK_BIT (1 << 3)

/* Window scale recorded when a side did not send the window scale option */
#define FW_TCP_NO_WSCALE 0xFF
/* Largest window scale allowed */
#define FW_TCP_MAX_WSCALE 14
/* Minimum window used when checking acknowledgements */
#define FW_TCP_MIN_ACK_WINDOW 66000

/* Interface recorded when no neighbour filter has claimed an instance */
#define FW_TCP_NO_IFACE 0xFF

/* Time before a deleted instance can be reused, so the neighbour filter is never
still updating it */
#define FW_TCP_REUSE_GRACE_NS (20 * NS_IN_S)

typedef enum {
    /* never used, ends a lookup */
    FW_TCP_SLOT_EMPTY = 0,
    /* holds a tracked connection */
    FW_TCP_SLOT_VALID,
    /* connection removed, slot may be reused after the grace period */
    FW_TCP_SLOT_DELETED
} fw_tcp_slot_state_t;

/* Tracking state for one side of a connection. Flags are written last and read
first, so the other filter always sees new flags with the fields written before
them. */
typedef struct fw_tcp_packet_data {
    /* flags of the last packet. Fin and rst remain set once sent */
    uint8_t flags;
    /* window scale sent in this side's syn, or FW_TCP_NO_WSCALE */
    uint8_t wscale;
    /* sequence number of the last packet, or of the fin once sent */
    uint32_t seq;
    /* last acknowledgement number */
    uint32_t ack;
    /* highest sequence number sent plus one */
    uint32_t end;
    /* largest scaled window advertised */
    uint32_t max_win;
    /* acknowledgement number plus window, how far the other side may send */
    uint32_t granted_end;
} fw_tcp_packet_data_t;

/* TCP filter specific instance */
typedef struct fw_tcp_instance {
    /* source ip of traffic, as seen by the owning filter */
    uint32_t src_ip;
    /* destination ip of traffic, as seen by the owning filter */
    uint32_t dst_ip;
    /* source port of traffic, as seen by the owning filter */
    uint16_t src_port;
    /* destination port of traffic, as seen by the owning filter */
    uint16_t dst_port;
    /* ID of the rule this instance was created from. Allows instances
    to be removed upon rule removal */
    uint16_t rule_id;
    /* fw_tcp_slot_state_t, accessed atomically */
    uint8_t slot_state;
    /* interface of the neighbour filter that writes the external tracking
    state, FW_TCP_NO_IFACE until claimed */
    uint8_t external_iface;
    /* tracking state of the owning filter's side */
    fw_tcp_packet_data_t internal;
    /* tracking state of the neighbour filter's side */
    fw_tcp_packet_data_t external;
    /* time in ns of the last permitted packet, or of deletion */
    uint64_t timestamp;
} fw_tcp_instance_t;

/* TCP instance table */
typedef struct fw_tcp_table {
    /* number of valid instances */
    uint16_t size;
    /* seed for hashing connections, set by the owning filter on start up */
    uint32_t hash_seed;
    fw_tcp_instance_t slots[];
} fw_tcp_table_t;

typedef enum {
    /* no traffic has been seen */
    TCP_NONE,
    /* this filter has received a syn */
    TCP_SYN_SENT,
    /* neighbour filter has received a syn */
    TCP_SYN_SEEN,
    /* both sides have sent a syn (simultaneous open) */
    TCP_SYN_BOTH,
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
    /* both sides have sent a fin, and this side's fin hasn't been acknowledged */
    TCP_FINACK_SENT,
    /* both sides have sent a fin, and only this side's fin has been acknowledged */
    TCP_FINACK_SEEN,
    /* both fins have been acknowledged */
    TCP_CLOSED,
    /* a valid rst has been sent by either side, no more traffic permitted */
    TCP_RESET
} fw_tcp_conn_state_t;

/* Details of a TCP packet used for tracking */
typedef struct fw_tcp_packet {
    bool syn;
    bool ack;
    bool fin;
    bool rst;
    /* sequence number */
    uint32_t seq;
    /* acknowledgement number */
    uint32_t ack_seq;
    /* sequence number following the packet. Syns and fins occupy one */
    uint32_t end;
    /* sequence number of fin, which follows any data */
    uint32_t fin_seq;
    /* unscaled window */
    uint16_t window;
    /* window scale option, only parsed for syns */
    uint8_t wscale;
} fw_tcp_packet_t;

/* Compare sequence numbers, which wrap around */
static inline bool fw_tcp_seq_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static inline bool fw_tcp_seq_after(uint32_t a, uint32_t b)
{
    return (int32_t)(b - a) < 0;
}

/* Create a TCP flag from a set of booleans */
static inline uint8_t fw_tcp_flags_to_bits(bool syn, bool ack, bool fin)
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

    return result;
}

/* Hash a connection, given from the owning filter's perspective */
static inline uint16_t fw_tcp_hash(uint32_t seed, uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                                   uint16_t dst_port, uint16_t capacity)
{
    uint32_t h = seed;
    h = (h ^ src_ip) * 16777619;
    h = (h ^ dst_ip) * 16777619;
    h = (h ^ (((uint32_t)src_port << 16) | dst_port)) * 16777619;
    return h % capacity;
}

/* Time since a timestamp, which may be slightly ahead if the neighbour filter
wrote it */
static inline uint64_t fw_tcp_elapsed(uint64_t now, uint64_t timestamp)
{
    return timestamp > now ? 0 : now - timestamp;
}

static inline fw_tcp_table_t *fw_tcp_table(fw_instances_table_t *table)
{
    return (fw_tcp_table_t *)table;
}

static inline fw_tcp_instance_t *fw_tcp_slots(fw_instances_table_t *table)
{
    return fw_tcp_table(table)->slots;
}

/**
 * Find a valid instance in an instance table.
 *
 * @param table instance table to search.
 * @param capacity capacity of the table.
 * @param src_ip source ip from the table owner's perspective.
 * @param src_port source port from the table owner's perspective.
 * @param dst_ip destination ip from the table owner's perspective.
 * @param dst_port destination port from the table owner's perspective.
 *
 * @return matching instance, or NULL if none exists.
 */
static inline fw_tcp_instance_t *fw_tcp_table_find(fw_instances_table_t *table, uint16_t capacity, uint32_t src_ip,
                                                   uint16_t src_port, uint32_t dst_ip, uint16_t dst_port)
{
    fw_tcp_instance_t *slots = fw_tcp_slots(table);
    uint16_t idx = fw_tcp_hash(fw_tcp_table(table)->hash_seed, src_ip, src_port, dst_ip, dst_port, capacity);
    for (uint16_t probes = 0; probes < capacity; probes++) {
        fw_tcp_instance_t *slot = slots + idx;
        uint8_t slot_state = __atomic_load_n(&slot->slot_state, __ATOMIC_ACQUIRE);
        if (slot_state == FW_TCP_SLOT_EMPTY) {
            return NULL;
        }

        if (slot_state == FW_TCP_SLOT_VALID && slot->src_ip == src_ip && slot->dst_ip == dst_ip
            && slot->src_port == src_port && slot->dst_port == dst_port) {
            return slot;
        }

        idx = (idx + 1 == capacity) ? 0 : idx + 1;
    }

    return NULL;
}

/**
 * Find the filter action to be applied for a given source and destination ip
 * and port number. Instances are checked first, then filter rules.
 *
 * @param state address of filter state.
 * @param src_ip source ip to match.
 * @param src_port source port to match.
 * @param dst_ip destination ip to match.
 * @param dst_port destination port to match.
 * @param rule_id id of matching rule.
 * @param iface interface of this filter.
 * @param tcp_instance matching instance. Unmodified if no match.
 * @param is_internal whether the matching instance is owned by this filter.
 *
 * @return filter action to be applied.
 */
static inline fw_action_t fw_tcp_filter_find_action(fw_filter_state_t *state, uint32_t src_ip, uint16_t src_port,
                                                    uint32_t dst_ip, uint16_t dst_port, uint16_t *rule_id,
                                                    uint8_t iface, fw_tcp_instance_t **tcp_instance,
                                                    bool *is_internal)
{
    fw_tcp_instance_t *instance = fw_tcp_table_find(state->internal_instances_table, state->instances_capacity,
                                                    src_ip, src_port, dst_ip, dst_port);
    if (instance != NULL) {
        *rule_id = instance->rule_id;
        *tcp_instance = instance;
        *is_internal = true;
        return FILTER_ACT_ESTABLISHED;
    }

    for (size_t i = 0; i < state->num_interfaces; i++) {
        instance = fw_tcp_table_find(state->external_instances_table[i], state->instances_capacity, dst_ip,
                                     dst_port, src_ip, src_port);
        if (instance == NULL) {
            continue;
        }

        /* Return traffic belongs to the interface which first received it */
        uint8_t claimed = __atomic_load_n(&instance->external_iface, __ATOMIC_ACQUIRE);
        if (claimed == FW_TCP_NO_IFACE || claimed == iface) {
            *rule_id = instance->rule_id;
            *tcp_instance = instance;
            *is_internal = false;
            return FILTER_ACT_ESTABLISHED;
        }
    }

    return fw_filter_match_rule(state, src_ip, src_port, dst_ip, dst_port, rule_id);
}

/* Internal client has sent a syn, external hasn't */
static inline bool fw_tcp_syn_sent(uint8_t internal_flags, uint8_t external_flags)
{
    return internal_flags == FW_TCP_SYN_BIT && !external_flags;
}

/* External client has sent a syn, internal has replied with a syn-ack */
static inline bool fw_tcp_synack_sent(uint8_t internal_flags, uint8_t external_flags)
{
    return internal_flags == (FW_TCP_SYN_BIT | FW_TCP_ACK_BIT) && external_flags == FW_TCP_SYN_BIT;
}

/* The syn handshake has completed and neither client has sent a fin */
static inline bool fw_tcp_established(uint8_t internal_flags, uint8_t external_flags)
{
    return !(internal_flags & FW_TCP_SYN_BIT) && (internal_flags & FW_TCP_ACK_BIT)
        && (external_flags & FW_TCP_ACK_BIT) && !(internal_flags & FW_TCP_FIN_BIT)
        && !(external_flags & FW_TCP_FIN_BIT);
}

/* Both clients have sent a syn-ack, completing a simultaneous open */
static inline bool fw_tcp_simultaneous_established(uint8_t internal_flags, uint8_t external_flags)
{
    return internal_flags == (FW_TCP_SYN_BIT | FW_TCP_ACK_BIT) && external_flags == (FW_TCP_SYN_BIT | FW_TCP_ACK_BIT);
}

/* Internal client has sent a fin and the external client has not */
static inline bool fw_tcp_fin_sent(uint8_t internal_flags, uint8_t external_flags)
{
    return (internal_flags & FW_TCP_FIN_BIT) && !(external_flags & FW_TCP_FIN_BIT);
}

/**
 * Determine the state of a connection from this filter's perspective.
 *
 * @param instance instance of the connection, NULL for a new connection.
 * @param is_internal whether the instance is owned by this filter.
 * @param internal_tracking set to the tracking state of this filter's side.
 * @param external_tracking set to the tracking state of the other side.
 * @param conn_state set to the connection state.
 *
 * @return error status.
 */
static inline fw_filter_err_t fw_tcp_extract_state(fw_tcp_instance_t *instance, bool is_internal,
                                                   fw_tcp_packet_data_t **internal_tracking,
                                                   fw_tcp_packet_data_t **external_tracking,
                                                   fw_tcp_conn_state_t *conn_state)
{
    /* No TCP state, new connection */
    if (instance == NULL) {
        *conn_state = TCP_NONE;
        return FILTER_ERR_OKAY;
    }

    *internal_tracking = is_internal ? &instance->internal : &instance->external;
    *external_tracking = is_internal ? &instance->external : &instance->internal;

    uint8_t internal_flags = __atomic_load_n(&(*internal_tracking)->flags, __ATOMIC_ACQUIRE);
    uint8_t external_flags = __atomic_load_n(&(*external_tracking)->flags, __ATOMIC_ACQUIRE);

    if ((internal_flags | external_flags) & FW_TCP_RST_BIT) {
        *conn_state = TCP_RESET;
    } else if (fw_tcp_established(internal_flags, external_flags)
               || fw_tcp_established(external_flags, internal_flags)
               || fw_tcp_simultaneous_established(internal_flags, external_flags)) {
        /* Most common case checked first */
        *conn_state = TCP_ESTABLISHED;
    } else if (fw_tcp_syn_sent(internal_flags, external_flags)) {
        *conn_state = TCP_SYN_SENT;
    } else if (fw_tcp_syn_sent(external_flags, internal_flags)) {
        *conn_state = TCP_SYN_SEEN;
    } else if (internal_flags == FW_TCP_SYN_BIT && external_flags == FW_TCP_SYN_BIT) {
        *conn_state = TCP_SYN_BOTH;
    } else if (fw_tcp_synack_sent(internal_flags, external_flags)) {
        *conn_state = TCP_SYNACK_SENT;
    } else if (fw_tcp_synack_sent(external_flags, internal_flags)) {
        *conn_state = TCP_SYNACK_SEEN;
    } else if (fw_tcp_fin_sent(internal_flags, external_flags)) {
        *conn_state = TCP_FIN_SENT;
    } else if (fw_tcp_fin_sent(external_flags, internal_flags)) {
        *conn_state = TCP_FIN_SEEN;
    } else if ((internal_flags & FW_TCP_FIN_BIT) && (external_flags & FW_TCP_FIN_BIT)) {
        /* Both sides have sent a fin, check which have been acknowledged */
        bool internal_fin_acked = (*external_tracking)->ack == (*internal_tracking)->seq + 1;
        bool external_fin_acked = (*internal_tracking)->ack == (*external_tracking)->seq + 1;
        if (internal_fin_acked && external_fin_acked) {
            *conn_state = TCP_CLOSED;
        } else if (internal_fin_acked) {
            *conn_state = TCP_FINACK_SEEN;
        } else {
            *conn_state = TCP_FINACK_SENT;
        }
    } else {
        return FILTER_ERR_INVALID_INSTANCE_STATE;
    }

    return FILTER_ERR_OKAY;
}

/**
 * Check a packet lies within the sequence and acknowledgement windows of the
 * connection.
 *
 * @param sender tracking state of the side which sent the packet.
 * @param receiver tracking state of the other side.
 * @param pkt packet to check.
 *
 * @return whether the packet is within the windows.
 */
static inline bool fw_tcp_in_window(fw_tcp_packet_data_t *sender, fw_tcp_packet_data_t *receiver,
                                    fw_tcp_packet_t *pkt)
{
    /* Data must start within the window the receiver has advertised */
    if (fw_tcp_seq_after(pkt->seq, receiver->granted_end)) {
        return false;
    }

    /* Data must not end before the oldest data the receiver could still accept */
    if (fw_tcp_seq_before(pkt->end, sender->end - receiver->max_win)) {
        return false;
    }

    if (pkt->ack) {
        /* Data which has not been sent cannot be acknowledged */
        if (fw_tcp_seq_after(pkt->ack_seq, receiver->end)) {
            return false;
        }

        /* Acknowledgements must not be older than a window */
        uint32_t ack_win = sender->max_win > FW_TCP_MIN_ACK_WINDOW ? sender->max_win : FW_TCP_MIN_ACK_WINDOW;
        if (fw_tcp_seq_before(pkt->ack_seq, receiver->end - ack_win)) {
            return false;
        }
    }

    return true;
}

/**
 * Check whether a rst is valid. Its sequence number must be the next one the
 * other side expects.
 *
 * @param conn_state state of the connection.
 * @param sender tracking state of the side which sent the rst.
 * @param receiver tracking state of the other side.
 * @param pkt rst packet.
 *
 * @return whether the rst is valid.
 */
static inline bool fw_tcp_rst_valid(fw_tcp_conn_state_t conn_state, fw_tcp_packet_data_t *sender,
                                    fw_tcp_packet_data_t *receiver, fw_tcp_packet_t *pkt)
{
    switch (conn_state) {
    case TCP_NONE:
        return false;
    case TCP_SYN_SENT:
        /* Other side has not sent anything, this side may abort its syn */
        return pkt->seq == sender->end;
    case TCP_SYN_SEEN:
        /* Refusing a connection */
        return pkt->ack && pkt->ack_seq == receiver->end;
    default:
        /* What the other side last acknowledged, or the end of what was sent */
        return pkt->seq == receiver->ack || pkt->seq == sender->end;
    }
}

/**
 * Check a packet from a side which has sent a fin. Only retransmissions and
 * acks are allowed.
 *
 * @param sender tracking state of the side which sent the packet.
 * @param receiver tracking state of the other side.
 * @param pkt packet to check.
 *
 * @return whether the packet is valid.
 */
static inline bool fw_tcp_after_fin_valid(fw_tcp_packet_data_t *sender, fw_tcp_packet_data_t *receiver,
                                          fw_tcp_packet_t *pkt)
{
    if (pkt->syn || !pkt->ack) {
        return false;
    }

    if (pkt->fin) {
        if (pkt->fin_seq != sender->seq) {
            return false;
        }
    } else if (pkt->end != pkt->seq) {
        /* Data must end before the fin */
        if (fw_tcp_seq_after(pkt->end, sender->seq)) {
            return false;
        }
    } else if (fw_tcp_seq_after(pkt->seq, sender->seq + 1)) {
        /* Acks can't come after the fin */
        return false;
    }

    return fw_tcp_in_window(sender, receiver, pkt);
}

/**
 * Record a permitted packet in the sender's tracking state. Once a fin has been
 * sent, its sequence number is kept.
 *
 * @param sender tracking state of the side which sent the packet.
 * @param receiver tracking state of the other side, only read.
 * @param pkt permitted packet.
 */
static inline void fw_tcp_record_packet(fw_tcp_packet_data_t *sender, fw_tcp_packet_data_t *receiver,
                                        fw_tcp_packet_t *pkt)
{
    bool first_ack = pkt->ack && !(sender->flags & FW_TCP_ACK_BIT);

    /* Window is only scaled if both sides sent the option, and never for syns */
    uint32_t win = pkt->window;
    if (pkt->syn) {
        sender->wscale = pkt->wscale;
        sender->end = pkt->end;
    } else {
        if (sender->wscale != FW_TCP_NO_WSCALE && receiver->wscale != FW_TCP_NO_WSCALE) {
            win <<= sender->wscale;
        }
        if (fw_tcp_seq_after(pkt->end, sender->end)) {
            sender->end = pkt->end;
        }
    }

    if (win > sender->max_win) {
        sender->max_win = win;
    }

    if (pkt->ack && (first_ack || fw_tcp_seq_after(pkt->ack_seq + win, sender->granted_end))) {
        sender->granted_end = pkt->ack_seq + win;
    }

    /* Retransmitted packets may have older acknowledgement numbers */
    if (pkt->ack && (first_ack || fw_tcp_seq_after(pkt->ack_seq, sender->ack))) {
        sender->ack = pkt->ack_seq;
    }

    uint8_t flags;
    if (sender->flags & FW_TCP_FIN_BIT) {
        flags = fw_tcp_flags_to_bits(false, pkt->ack, true);
    } else {
        flags = fw_tcp_flags_to_bits(pkt->syn, pkt->ack, pkt->fin);
        sender->seq = pkt->fin ? pkt->fin_seq : pkt->seq;
    }
    __atomic_store_n(&sender->flags, flags, __ATOMIC_RELEASE);
}

/**
 * Reserve a slot for a new instance. Other filters can't see it until it is
 * published with fw_tcp_filter_publish_instance.
 *
 * @param state address of filter state.
 * @param src_ip source ip of instance traffic.
 * @param src_port source port of instance traffic.
 * @param dst_ip destination ip of instance traffic.
 * @param dst_port destination port of instance traffic.
 * @param rule_id id of connect rule.
 * @param now current time in ns.
 * @param return_instance reserved instance.
 *
 * @return error status.
 */
static inline fw_filter_err_t fw_tcp_filter_add_instance(fw_filter_state_t *state, uint32_t src_ip,
                                                         uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
                                                         uint16_t rule_id, uint64_t now,
                                                         fw_tcp_instance_t **return_instance)
{
    uint16_t capacity = state->instances_capacity;
    fw_tcp_instance_t *slots = fw_tcp_slots(state->internal_instances_table);
    uint16_t idx = fw_tcp_hash(fw_tcp_table(state->internal_instances_table)->hash_seed, src_ip, src_port, dst_ip,
                               dst_port, capacity);

    fw_tcp_instance_t *free_slot = NULL;
    for (uint16_t probes = 0; probes < capacity && free_slot == NULL; probes++) {
        fw_tcp_instance_t *slot = slots + idx;
        if (slot->slot_state == FW_TCP_SLOT_EMPTY
            || (slot->slot_state == FW_TCP_SLOT_DELETED
                && fw_tcp_elapsed(now, __atomic_load_n(&slot->timestamp, __ATOMIC_RELAXED))
                       >= FW_TCP_REUSE_GRACE_NS)) {
            free_slot = slot;
        }
        idx = (idx + 1 == capacity) ? 0 : idx + 1;
    }

    if (free_slot == NULL) {
        return FILTER_ERR_FULL;
    }

    free_slot->src_ip = src_ip;
    free_slot->src_port = src_port;
    free_slot->dst_ip = dst_ip;
    free_slot->dst_port = dst_port;
    free_slot->rule_id = rule_id;
    free_slot->external_iface = FW_TCP_NO_IFACE;
    free_slot->internal = (fw_tcp_packet_data_t) { .wscale = FW_TCP_NO_WSCALE };
    free_slot->external = (fw_tcp_packet_data_t) { .wscale = FW_TCP_NO_WSCALE };
    free_slot->timestamp = now;
    *return_instance = free_slot;
    return FILTER_ERR_OKAY;
}

/* Make a reserved instance visible to other filters */
static inline void fw_tcp_filter_publish_instance(fw_filter_state_t *state, fw_tcp_instance_t *instance)
{
    __atomic_store_n(&instance->slot_state, FW_TCP_SLOT_VALID, __ATOMIC_RELEASE);
    state->internal_instances_table->size++;
}

/**
 * Remove an instance owned by this filter. The slot isn't reused until
 * FW_TCP_REUSE_GRACE_NS has passed.
 *
 * @param state address of filter state.
 * @param instance instance to remove.
 * @param now current time in ns.
 */
static inline void fw_tcp_filter_remove_instance(fw_filter_state_t *state, fw_tcp_instance_t *instance,
                                                 uint64_t now)
{
    __atomic_store_n(&instance->timestamp, now, __ATOMIC_RELAXED);
    __atomic_store_n(&instance->slot_state, FW_TCP_SLOT_DELETED, __ATOMIC_RELEASE);
    state->internal_instances_table->size--;
}

/**
 * Remove an instance owned by this filter which has timed out. Fails if the
 * neighbour filter has updated the timestamp since it was checked.
 *
 * @param state address of filter state.
 * @param instance instance to remove.
 * @param timestamp timestamp that was checked.
 * @param now current time in ns.
 *
 * @return whether the instance was removed.
 */
static inline bool fw_tcp_filter_expire_instance(fw_filter_state_t *state, fw_tcp_instance_t *instance,
                                                 uint64_t timestamp, uint64_t now)
{
    __atomic_store_n(&instance->slot_state, FW_TCP_SLOT_DELETED, __ATOMIC_SEQ_CST);
    if (!__atomic_compare_exchange_n(&instance->timestamp, &timestamp, now, false, __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST)) {
        /* Traffic arrived after the timeout was checked, keep the connection */
        __atomic_store_n(&instance->slot_state, FW_TCP_SLOT_VALID, __ATOMIC_RELEASE);
        return false;
    }

    state->internal_instances_table->size--;
    return true;
}

/**
 * Turn deleted slots followed by an empty slot back into empty slots, to keep
 * lookups short.
 *
 * @param state address of filter state.
 * @param now current time in ns.
 */
static inline void fw_tcp_filter_clear_deleted(fw_filter_state_t *state, uint64_t now)
{
    uint16_t capacity = state->instances_capacity;
    fw_tcp_instance_t *slots = fw_tcp_slots(state->internal_instances_table);

    /* Go backwards through the table twice to handle wrapping around the end */
    for (uint32_t step = 0; step < 2 * (uint32_t)capacity; step++) {
        uint16_t idx = capacity - 1 - (step % capacity);
        uint16_t next = (idx + 1 == capacity) ? 0 : idx + 1;
        if (slots[idx].slot_state == FW_TCP_SLOT_DELETED && slots[next].slot_state == FW_TCP_SLOT_EMPTY
            && fw_tcp_elapsed(now, __atomic_load_n(&slots[idx].timestamp, __ATOMIC_RELAXED))
                   >= FW_TCP_REUSE_GRACE_NS) {
            __atomic_store_n(&slots[idx].slot_state, FW_TCP_SLOT_EMPTY, __ATOMIC_RELEASE);
        }
    }
}

/**
 * Claim an instance for this filter's interface.
 *
 * @param instance instance owned by another filter.
 * @param iface interface of this filter.
 *
 * @return whether this filter holds the claim.
 */
static inline bool fw_tcp_filter_claim_instance(fw_tcp_instance_t *instance, uint8_t iface)
{
    uint8_t expected = FW_TCP_NO_IFACE;
    if (__atomic_compare_exchange_n(&instance->external_iface, &expected, iface, false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
        return true;
    }

    return expected == iface;
}
