/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/icmp.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".fw_filter_config"))) fw_filter_config_t filter_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

/* Queues for receiving and transmitting packets */
net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;
fw_queue_t router_queue;
fw_queue_t icmp_queue;

/* Holds filtering rules and state */
fw_filter_state_t filter_state;

#define ICMP_FILTER_DUMMY_PORT 0

/* ICMP request queue to send unreachable messages to ICMP module */
static bool notify_icmp;

static bool enqueue_icmp_unreachable(net_buff_desc_t buffer)
{
    uintptr_t pkt_vaddr = (uintptr_t)(net_config.rx_data.vaddr + buffer.io_or_offset);
    bool enqueued = icmp_enqueue_error(&icmp_queue, ICMP_DEST_UNREACHABLE, ICMP_DEST_PORT_UNREACHABLE, pkt_vaddr,
                                       filter_config.interface);
    notify_icmp |= enqueued;
    return enqueued;
}

static void filter(void)
{
    bool transmitted = false;
    bool returned = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            uintptr_t pkt_vaddr = (uintptr_t)(net_config.rx_data.vaddr + buffer.io_or_offset);
            ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);

            uint16_t rule_id = 0;
            fw_action_t action = fw_filter_find_action(&filter_state, ip_hdr->src_ip, ICMP_FILTER_DUMMY_PORT,
                                                       ip_hdr->dst_ip, ICMP_FILTER_DUMMY_PORT, &rule_id);

            switch (action) {
            case FILTER_ACT_CONNECT: {
                /* Add an established connection in shared memory for corresponding filter */
                fw_filter_err_t fw_err = fw_filter_add_instance(&filter_state, ip_hdr->src_ip, ICMP_FILTER_DUMMY_PORT,
                                                                ip_hdr->dst_ip, ICMP_FILTER_DUMMY_PORT, rule_id);

                if ((fw_err == FILTER_ERR_OKAY || fw_err == FILTER_ERR_DUPLICATE) && FW_DEBUG_OUTPUT) {
                    sddf_printf(
                        "ICMP FILTER LOG: on interface %u establishing connection via rule %u: (ip %s, port %u) -> "
                        "(ip %s, port %u)\n",
                        filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                        ICMP_FILTER_DUMMY_PORT, ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), ICMP_FILTER_DUMMY_PORT);
                }

                if (fw_err == FILTER_ERR_FULL) {
                    sddf_printf("ICMP FILTER LOG: on interface %u could not establish connection for rule %u: (ip %s, "
                                "port %u) -> (ip %s, port %u): %s\n",
                                filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                                ICMP_FILTER_DUMMY_PORT, ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                                ICMP_FILTER_DUMMY_PORT, fw_filter_err_str[fw_err]);
                }
            }
            case FILTER_ACT_ESTABLISHED:
            case FILTER_ACT_ALLOW: {
                /* Transmit the packet to the routing component */
                /* Reset the checksum if it's recalculated in hardware */
#ifdef NETWORK_HW_HAS_CHECKSUM
                icmp_hdr_t *icmp_hdr = (icmp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));
                icmp_hdr->check = 0;
#endif

                err = fw_enqueue(&router_queue, &buffer);
                assert(!err);
                transmitted = true;

                if (FW_DEBUG_OUTPUT) {
                    if (action == FILTER_ACT_ALLOW || action == FILTER_ACT_CONNECT) {
                        sddf_printf(
                            "ICMP FILTER LOG: on interface %u transmitting via rule %u: (ip %s, port %u) -> (ip %s, "
                            "port %u)\n",
                            filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                            ICMP_FILTER_DUMMY_PORT, ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                            ICMP_FILTER_DUMMY_PORT);
                    } else if (action == FILTER_ACT_ESTABLISHED) {
                        sddf_printf(
                            "ICMP FILTER LOG: on interface %u transmitting via external rule %u: (ip %s, port %u) "
                            "-> (ip %s, port %u)\n",
                            filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                            ICMP_FILTER_DUMMY_PORT, ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                            ICMP_FILTER_DUMMY_PORT);
                    }
                }
                break;
            }
            case FILTER_ACT_REJECT: {
                icmp_hdr_t *icmp_hdr = (icmp_hdr_t *)(pkt_vaddr + ICMP_HDR_OFFSET);
                uint8_t icmp_type = icmp_hdr->type;
                /* An ICMP error message shouldn't be sent upon reception of another ICMP error message */
                if (!icmp_is_error_message(icmp_type)) {
                    /* Enqueue an ICMP port unreachable message */
                    enqueue_icmp_unreachable(buffer);
                }

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("ICMP FILTER LOG: on interface %u, filter rejecting via rule %u: (ip %s, port %u) -> "
                                "(ip %s, port %u)\n",
                                filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                                ICMP_FILTER_DUMMY_PORT, ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1),
                                ICMP_FILTER_DUMMY_PORT);
                }
            }
            case FILTER_ACT_DROP:
            default: {
                /* Return the buffer to the rx virtualiser */
                err = net_enqueue_free(&rx_queue, buffer);
                assert(!err);
                returned = true;

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf(
                        "ICMP FILTER LOG: on interface %u dropping via rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                        filter_config.interface, rule_id, ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0),
                        ICMP_FILTER_DUMMY_PORT, ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), ICMP_FILTER_DUMMY_PORT);
                }
                break;
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

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    switch (microkit_msginfo_get_label(msginfo)) {
    case FILTER_SET_DEFAULT_ACTION: {
        fw_action_t action = microkit_mr_get(FILTER_SET_DEFAULT_ARG_ACTION);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("ICMP FILTER LOG: on interface %u changing default action from %u to %u\n",
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
        uint32_t dst_ip = microkit_mr_get(FILTER_ADD_ARG_DST_IP);
        uint8_t src_subnet = microkit_mr_get(FILTER_ADD_ARG_SRC_SUBNET);
        uint8_t dst_subnet = microkit_mr_get(FILTER_ADD_ARG_DST_SUBNET);

        /* ICMP filter does not support this action */
        if (action == 0 || action > FW_FILTER_NUM_ACTIONS || !filter_config.webserver.actions[action - 1]) {
            microkit_mr_set(FILTER_RET_ERR, FILTER_ERR_UNSUPPORTED_ACTION);
            return microkit_msginfo_new(0, 1);
        }

        uint16_t rule_id = 0;
        fw_filter_err_t err = fw_filter_add_rule(&filter_state, src_ip, ICMP_FILTER_DUMMY_PORT, dst_ip,
                                                 ICMP_FILTER_DUMMY_PORT, src_subnet, dst_subnet, true, true, action,
                                                 &rule_id);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf(
                "ICMP FILTER LOG: on interface %u create rule %u: (ip %s, mask %u, port %u, any_port %u) - (%s) -> "
                "(ip %s, mask %u, port %u, any_port %u): %s\n",
                filter_config.interface, rule_id, ipaddr_to_string(src_ip, ip_addr_buf0), src_subnet,
                ICMP_FILTER_DUMMY_PORT, false, fw_filter_action_str[action], ipaddr_to_string(dst_ip, ip_addr_buf1),
                dst_subnet, ICMP_FILTER_DUMMY_PORT, false, fw_filter_err_str[err]);
        }

        microkit_mr_set(FILTER_RET_ERR, err);
        microkit_mr_set(FILTER_RET_RULE_ID, rule_id);
        return microkit_msginfo_new(0, 2);
    }
    case FILTER_DEL_RULE: {
        uint16_t rule_id = microkit_mr_get(FILTER_DELETE_ARG_RULE_ID);
        fw_filter_err_t err = fw_filter_remove_rule(&filter_state, rule_id);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("ICMP FILTER LOG: on interface %u remove rule id %u: %s\n", filter_config.interface, rule_id,
                        fw_filter_err_str[err]);
        }

        microkit_mr_set(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        sddf_printf("ICMP FILTER LOG: on interface %u, unknown request %lu on channel %u\n", filter_config.interface,
                    microkit_msginfo_get_label(msginfo), ch);
        break;
    }

    return microkit_msginfo_new(0, 0);
}

void notified(microkit_channel ch)
{
    if (ch == net_config.rx.id) {
        filter();
    } else {
        sddf_dprintf("ICMP FILTER LOG: on interface %u, received notification on unknown channel: %d!\n",
                     filter_config.interface, ch);
    }

    if (notify_icmp) {
        sddf_printf("ICMP FILTER LOG: on interface %u filter notifying ICMP module on channel %u\n",
                    filter_config.interface, filter_config.icmp_module.ch);
        notify_icmp = false;
        microkit_notify(filter_config.icmp_module.ch);
    }
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);

    fw_queue_init(&router_queue, filter_config.router.queue.vaddr, sizeof(net_buff_desc_t),
                  filter_config.router.capacity);

    fw_queue_init(&icmp_queue, filter_config.icmp_module.queue.vaddr, sizeof(icmp_req_t),
                  filter_config.icmp_module.capacity);

    fw_filter_state_init(&filter_state, filter_config.webserver.rules.vaddr, filter_config.rule_id_bitmap.vaddr,
                         filter_config.webserver.rules_capacity, filter_config.internal_instances.vaddr,
                         filter_config.external_instances, filter_config.instances_capacity,
                         filter_config.initial_rules, filter_config.num_initial_rules,
                         filter_config.num_external_instances);
}
