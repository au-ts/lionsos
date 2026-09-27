/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <lions/firewall/checksum.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/tcp.h>
#include <os/sddf.h>
#include <sddf/network/config.h>
#include <sddf/network/queue.h>
#include <sddf/util/printf.h>
#include <sddf/util/util.h>
#include <stdbool.h>
#include <stdint.h>

__attribute__((__section__(".fw_filter_config"))) fw_filter_config_t filter_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;


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
static inline bool fw_tcp_instance_match(const fw_instance_t *instance, uint32_t src_ip, uint16_t src_port,
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
                                             fw_instance_t **instance) {
    /* We give priority to local instances */
    for (uint16_t i = 0; i < state->internal_instances_table->size; i++) {
        fw_instance_t *curr_instance = (fw_instance_t *)((uint8_t *)state->internal_instances_table->instances +
                                                                 (i * sizeof(fw_instance_t)));
        if (!fw_tcp_instance_match(curr_instance, src_ip, src_port, dst_ip, dst_port)) {
            continue;
        }

        *rule_id = curr_instance->rule_id;
        if (instance) {
            *instance = curr_instance;
        }
        return FILTER_ACT_ESTABLISHED;
    }

    for (uint8_t iface = 0; iface < state->num_interfaces; iface++) {
        fw_instances_table_t *ext_table = state->external_instances_table[iface];
        for (uint16_t i = 0; i < ext_table->size; i++) {
            fw_instance_t *curr_instance = &ext_table->instances[i];
            if (!fw_tcp_instance_match(curr_instance, src_ip, src_port, dst_ip, dst_port)) {
                continue;
            }

            *rule_id = curr_instance->rule_id;
            if (instance) {
                *instance = curr_instance;
            }
            return FILTER_ACT_ESTABLISHED;
        }
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
    case TCP_INVALID:
        return TCP_INVALID;
    }

    return TCP_INVALID;
}

/* Instance creation now goes straight through filter.h's fw_filter_add_instance
 * (declared in filter.h) instead of a local TCP-specific wrapper.
 * NOTE: filter.h's version does NOT do the stale-instance (TCP_NONE /
 * TCP_TIME_WAIT) sweep-and-reclaim that the removed wrapper did — closed
 * connection slots are no longer proactively recycled here, so the internal
 * instances table will fill up faster under churn until a timer-driven reaper
 * is added elsewhere. */

/* Queues for receiving and transmitting packets */
net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;
fw_queue_t router_queue;

/* Holds filtering rules and state */
fw_filter_state_t filter_state;

/* Current tick, used to track aging instances */
// Courtney: This has not yet been implemented, i.e. the TCP filter does not yet
// have access to the timer driver and thus does not receive ticks.
uint64_t curr_tick = 0;

static void filter(void) {
    bool transmitted = false;
    bool returned = false;
    bool reprocess = true;
    int enqueue_err;

    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            uintptr_t pkt_vaddr = (uintptr_t)(net_config.rx_data.vaddr + buffer.io_or_offset);
            ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
            tcp_hdr_t *tcp_hdr = (tcp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));

            uint16_t rule_id = 0;
            fw_instance_t *instance = NULL;
            fw_action_t action = fw_tcp_filter_find_action(&filter_state, ip_hdr->src_ip, tcp_hdr->src_port,
                                                           ip_hdr->dst_ip, tcp_hdr->dst_port, &rule_id, &instance);

            switch (action) {
            case FILTER_ACT_CONNECT: {
                uint32_t initial_seq = ntohl(tcp_hdr->seq);

                /* Add an established connection in shared memory for corresponding filter */
                fw_filter_err_t fw_err =
                    fw_filter_add_instance(&filter_state, ip_hdr->src_ip, tcp_hdr->src_port, ip_hdr->dst_ip,
                                           tcp_hdr->dst_port, rule_id, initial_seq);

                if ((fw_err == FILTER_ERR_OKAY || fw_err == FILTER_ERR_DUPLICATE)) {
                    LOG_FIREWALL("TCP FILTER", "on interface %u establishing connection via rule %u: (ip %s, port %u) -> "
                        "(ip %s, port %u)\n",
                        filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                        htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                        htons(tcp_hdr->dst_port));
                }

                if (fw_err == FILTER_ERR_FULL) {
                    LOG_FIREWALL("TCP FILTER", "on interface %u could not establish connection for rule %u: (ip %s, "
                                "port %u) -> (ip %s, port %u): %s\n",
                                filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                                htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                                htons(tcp_hdr->dst_port), fw_filter_err_str[fw_err]);
                    goto drop_packet;
                }
                /* fall through: a successful FILTER_ACT_CONNECT should also transmit this packet */
            }
            case FILTER_ACT_ESTABLISHED:
            case FILTER_ACT_ALLOW: {
                /* Transmit the packet to the routing component */
                /* Reset the checksum if it's recalculated in hardware */
#ifdef NETWORK_HW_HAS_CHECKSUM
                tcp_hdr->check = 0;
#endif

                enqueue_err = fw_enqueue(&router_queue, &buffer);
                assert(!enqueue_err);
                transmitted = true;

                if (instance != NULL) {
                    instance->timestamp = curr_tick;
                }

                if (action == FILTER_ACT_ALLOW || action == FILTER_ACT_CONNECT) {
                    LOG_FIREWALL("TCP FILTER", "on interface %u transmitting via rule %u: (ip %s, port %u) -> (ip %s, "
                        "port %u)\n",
                        filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                        htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                        htons(tcp_hdr->dst_port));
                } else if (action == FILTER_ACT_ESTABLISHED) {
                    LOG_FIREWALL("TCP FILTER", "on interface %u transmitting via external rule %u: (ip %s, port %u) -> "
                        "(ip %s, port %u)\n",
                        filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                        htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                        htons(tcp_hdr->dst_port));
                }
                break;
            }
            case FILTER_ACT_DROP:
            default: {
            drop_packet:
                /* Return the buffer to the rx virtualiser */
                enqueue_err = net_enqueue_free(&rx_queue, buffer);
                assert(!enqueue_err);
                returned = true;

                LOG_FIREWALL("TCP FILTER", "on interface %u dropping via rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                    filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                    htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                    htons(tcp_hdr->dst_port));

                break;
            }
            }

            // Handle state transitions on packets that matched a tracking instance
            if (instance != NULL) {
                // Determine direction relative to the original connection initiator
                bool is_forward = (instance->src_ip == ip_hdr->src_ip && instance->src_port == tcp_hdr->src_port);

                uint32_t packet_seq = ntohl(tcp_hdr->seq);
                uint8_t flags = tcp_hdr->flags;

                // Payload length tracking calculations
                uint16_t ip_hdr_len = ipv4_header_length(ip_hdr);
                uint16_t tcp_hdr_len = (tcp_hdr->doff) * 4;
                uint32_t payload_len = ntohs(ip_hdr->tot_len) - ip_hdr_len - tcp_hdr_len;

                if (is_forward) {
                    uint32_t control_adjustment = ((flags & FW_TCP_SYN_BIT) || (flags & FW_TCP_FIN_BIT)) ? 1 : 0;
                    instance->local_next_seq = packet_seq + payload_len + control_adjustment;
                } else {
                    uint32_t control_adjustment = ((flags & FW_TCP_SYN_BIT) || (flags & FW_TCP_FIN_BIT)) ? 1 : 0;
                    instance->extern_next_seq = packet_seq + payload_len + control_adjustment;
                }

                fw_tcp_conn_state_t new_state = fw_tcp_next_state(instance->current_state, flags, is_forward);
                if (new_state == TCP_INVALID) {
                    /* Flag for deletion */
                    action = FILTER_ACT_DROP;
                } else {
                    instance->current_state = new_state;
                    if (new_state != TCP_NONE) {
                        // Track historical telemetry data, we don't do it if none for debugging purposes
                        if (is_forward) {
                            instance->local.flags = flags;
                            instance->local.seq = packet_seq;
                        } else {
                            instance->external.flags = flags;
                            instance->external.seq = packet_seq;
                        }
                    }
                }
            }
        }

        net_request_signal_active(&rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue)) {
            net_cancel_signal_active(&rx_queue);
            reprocess = true;
        }
    }

    if (returned) {
        microkit_deferred_notify(net_config.rx.id);
    }

    if (transmitted) {
        microkit_notify(filter_config.router.ch);
    }
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    switch (microkit_msginfo_get_label(msginfo)) {
    case FILTER_SET_DEFAULT_ACTION: {
        fw_action_t action = microkit_mr_get(FILTER_SET_DEFAULT_ARG_ACTION);

        LOG_FIREWALL("TCP FILTER", "on interface %u changing default action from %u to %u\n",
                    filter_config.interface, filter_state.rule_table->rules[DEFAULT_ACTION_IDX].action, action);


        fw_filter_err_t err = fw_filter_update_default_action(&filter_state, action);
        assert(err == FILTER_ERR_OKAY);

        microkit_mr_set(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FILTER_ADD_RULE: {
        fw_action_t action = microkit_mr_get(FILTER_ADD_ARG_ACTION);
        uint32_t src_ip = microkit_mr_get(FILTER_ADD_ARG_SRC_IP);
        uint16_t src_port = microkit_mr_get(FILTER_ADD_ARG_SRC_PORT);
        uint32_t dst_ip = microkit_mr_get(FILTER_ADD_ARG_DST_IP);
        uint16_t dst_port = microkit_mr_get(FILTER_ADD_ARG_DST_PORT);
        uint8_t src_subnet = microkit_mr_get(FILTER_ADD_ARG_SRC_SUBNET);
        uint8_t dst_subnet = microkit_mr_get(FILTER_ADD_ARG_DST_SUBNET);
        bool src_port_any = microkit_mr_get(FILTER_ADD_ARG_SRC_ANY_PORT);
        bool dst_port_any = microkit_mr_get(FILTER_ADD_ARG_DST_ANY_PORT);

        /* TCP filter does not support this action */
        if (action == 0 || action > FW_FILTER_NUM_ACTIONS || !filter_config.webserver.actions[action - 1]) {
            microkit_mr_set(FILTER_RET_ERR, FILTER_ERR_UNSUPPORTED_ACTION);
            return microkit_msginfo_new(0, 1);
        }

        uint16_t rule_id = 0;
        fw_filter_err_t err = fw_filter_add_rule(&filter_state, src_ip, src_port, dst_ip, dst_port, src_subnet,
                                                 dst_subnet, src_port_any, dst_port_any, action, &rule_id);

        LOG_FIREWALL("TCP FILTER", "on interface %u create rule %u: (ip %s, mask %u, port %u, any_port %u) - (%s) -> "
            "(ip %s, mask %u, port %u, any_port %u): %s\n",
            filter_config.interface, rule_id, ipaddr_to_string(src_ip, ip_addr_buf0), src_subnet, htons(src_port),
            src_port_any, fw_filter_action_str[action], ipaddr_to_string(dst_ip, ip_addr_buf1), dst_subnet,
            htons(dst_port), dst_port_any, fw_filter_err_str[err]);

        microkit_mr_set(FILTER_RET_ERR, err);
        microkit_mr_set(FILTER_RET_RULE_ID, rule_id);
        return microkit_msginfo_new(0, 2);
    }
    case FILTER_DEL_RULE: {
        uint16_t rule_id = microkit_mr_get(FILTER_DELETE_ARG_RULE_ID);
        fw_filter_err_t err = fw_filter_remove_rule(&filter_state, rule_id);

        LOG_FIREWALL("TCP FILTER", "on interface %u remove rule id %u: %s\n", filter_config.interface, rule_id,
                    fw_filter_err_str[err]);

        microkit_mr_set(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        LOG_FIREWALL("TCP FILTER", "on interface %u unknown request %lu on channel %u\n", filter_config.interface,
                    microkit_msginfo_get_label(msginfo), ch);
        break;
    }

    return microkit_msginfo_new(0, 0);
}

void notified(microkit_channel ch) {
    if (ch == net_config.rx.id) {
        filter();
    } else {
        sddf_dprintf("TCP FILTER LOG: on interface %u, received notification on unknown channel: %d!\n",
                     filter_config.interface, ch);
    }
}

void init(void) {
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);

    fw_queue_init(&router_queue, filter_config.router.queue.vaddr, sizeof(net_buff_desc_t),
                  filter_config.router.capacity);

    fw_filter_state_init(&filter_state, filter_config.webserver.rules.vaddr, filter_config.rule_id_bitmap.vaddr,
                         filter_config.webserver.rules_capacity, filter_config.internal_instances.vaddr,
                         filter_config.external_instances, filter_config.instances_capacity,
                         filter_config.initial_rules, filter_config.num_initial_rules,
                         filter_config.num_external_instances);
}