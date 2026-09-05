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
#include <lions/firewall/tcp_filter.h>
#include <os/sddf.h>
#include <sddf/network/config.h>
#include <sddf/network/queue.h>
#include <sddf/util/printf.h>
#include <sddf/util/util.h>
#include <stdbool.h>
#include <stdint.h>

__attribute__((__section__(".fw_filter_config"))) fw_filter_config_t filter_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

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

// Helper fn to allocate the tracking instance slot
static inline fw_filter_err_t fw_tcp_create_and_bind_instance(fw_filter_state_t *state, ipv4_hdr_t *ip_hdr,
                                                              tcp_hdr_t *tcp_hdr, uint16_t rule_id,
                                                              fw_tcp_instance_t **instance) {
    uint32_t initial_seq = ntohl(tcp_hdr->seq);
    fw_filter_err_t fw_err = fw_filter_add_instance(state, ip_hdr->src_ip, tcp_hdr->src_port, ip_hdr->dst_ip,
                                                    tcp_hdr->dst_port, rule_id, initial_seq);
    // If the slot was successfully created or already existed, link the pointer
    if (fw_err == FILTER_ERR_OKAY || fw_err == FILTER_ERR_DUPLICATE) {
        // Dummy id used,
        uint16_t dummy_rule_id;
        fw_tcp_filter_find_action(state, ip_hdr->src_ip, tcp_hdr->src_port, ip_hdr->dst_ip, tcp_hdr->dst_port,
                                  &dummy_rule_id, instance);
    }

    return fw_err;
}

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
            fw_tcp_instance_t *instance = NULL;
            fw_action_t action = fw_tcp_filter_find_action(&filter_state, ip_hdr->src_ip, tcp_hdr->src_port,
                                                           ip_hdr->dst_ip, tcp_hdr->dst_port, &rule_id, &instance);

            switch (action) {
            case FILTER_ACT_CONNECT: {
                uint32_t initial_seq = ntohl(tcp_hdr->seq);

                /* Add an established connection in shared memory for corresponding filter */
                fw_filter_err_t fw_err =
                    fw_filter_add_instance(&filter_state, ip_hdr->src_ip, tcp_hdr->src_port, ip_hdr->dst_ip,
                                           tcp_hdr->dst_port, rule_id, initial_seq);

                if ((fw_err == FILTER_ERR_OKAY || fw_err == FILTER_ERR_DUPLICATE) && FW_DEBUG_OUTPUT) {
                    sddf_printf(
                        "TCP FILTER LOG: on interface %u establishing connection via rule %u: (ip %s, port %u) -> "
                        "(ip %s, port %u)\n",
                        filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                        htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                        htons(tcp_hdr->dst_port));
                }

                if (fw_err == FILTER_ERR_FULL) {
                    sddf_printf("TCP FILTER LOG: on interface %u could not establish connection for rule %u: (ip %s, "
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

                if (FW_DEBUG_OUTPUT) {
                    if (action == FILTER_ACT_ALLOW || action == FILTER_ACT_CONNECT) {
                        sddf_printf(
                            "TCP FILTER LOG: on interface %u transmitting via rule %u: (ip %s, port %u) -> (ip %s, "
                            "port %u)\n",
                            filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                            htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                            htons(tcp_hdr->dst_port));
                    } else if (action == FILTER_ACT_ESTABLISHED) {
                        sddf_printf(
                            "TCP FILTER LOG: on interface %u transmitting via external rule %u: (ip %s, port %u) -> "
                            "(ip %s, port %u)\n",
                            filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                            htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                            htons(tcp_hdr->dst_port));
                    }
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

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf(
                        "TCP FILTER LOG: on interface %u dropping via rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                        filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                        htons(tcp_hdr->src_port), ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                        htons(tcp_hdr->dst_port));
                }
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
                uint32_t payload_len = ntohs(ip_hdr->total_len) - ip_hdr_len - tcp_hdr_len;

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

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("TCP FILTER LOG: on interface %u changing default action from %u to %u\n",
                        filter_config.interface, filter_state.rule_table->rules[DEFAULT_ACTION_IDX].action, action);
        }

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

        if (FW_DEBUG_OUTPUT) {
            sddf_printf(
                "TCP FILTER LOG: on interface %u create rule %u: (ip %s, mask %u, port %u, any_port %u) - (%s) -> "
                "(ip %s, mask %u, port %u, any_port %u): %s\n",
                filter_config.interface, rule_id, ipaddr_to_string(src_ip, ip_addr_buf0), src_subnet, htons(src_port),
                src_port_any, fw_filter_action_str[action], ipaddr_to_string(dst_ip, ip_addr_buf1), dst_subnet,
                htons(dst_port), dst_port_any, fw_filter_err_str[err]);
        }

        microkit_mr_set(FILTER_RET_ERR, err);
        microkit_mr_set(FILTER_RET_RULE_ID, rule_id);
        return microkit_msginfo_new(0, 2);
    }
    case FILTER_DEL_RULE: {
        uint16_t rule_id = microkit_mr_get(FILTER_DELETE_ARG_RULE_ID);
        fw_filter_err_t err = fw_filter_remove_rule(&filter_state, rule_id);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("TCP FILTER LOG: on interface %u remove rule id %u: %s\n", filter_config.interface, rule_id,
                        fw_filter_err_str[err]);
        }

        microkit_mr_set(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        sddf_printf("TCP FILTER LOG: on interface %u unknown request %lu on channel %u\n", filter_config.interface,
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