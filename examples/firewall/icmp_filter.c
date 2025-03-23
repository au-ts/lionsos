/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/network/util.h>
#include <lions/firewall/config.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".firewall_filter_config"))) firewall_filter_config_t filter_config;

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

/* Queues for receiving and transmitting packets */
net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;
firewall_queue_handle_t router_queue;

/* Holds filtering rules and state */
firewall_filter_state_t filter_state;

#define ICMP_FILTER_DUMMY_PORT 1

void filter(void)
{
    bool transmitted = false;
    bool returned = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            void *pkt_vaddr = net_config.rx_data.vaddr + buffer.io_or_offset;
            ipv4_packet_t *ip_pkt = (ipv4_packet_t *)pkt_vaddr;

            bool default_action = false;
            uint8_t rule_id = 0;
            firewall_action_t action = firewall_filter_find_action(&filter_state, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT,
                                                                   ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT, &rule_id);

            /* Perform the default action */
            if (action == FILTER_ACT_NONE) {
                default_action = true;
                action = filter_state.default_action;
                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | ICMP filter found no match, performing default action %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                        filter_config.mac_addr[5], action, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT, ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT);
                }
            }
            
            /* Add an established connection in shared memory for corresponding filter */
            if (action == FILTER_ACT_CONNECT) {
                firewall_filter_error_t fw_err = firewall_filter_add_instance(&filter_state, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT,
                                                                                ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT, default_action, rule_id);

                if ((fw_err == FILTER_ERR_OKAY || fw_err == FILTER_ERR_DUPLICATE) && FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | ICMP filter establishing connection via rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                        filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT, ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT);
                } else if (fw_err == FILTER_ERR_FULL) {
                    sddf_printf("ICMP_FILTER|LOG: could not establish connection (full) for rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                        rule_id, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT, ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT);
                }
            }

            /* Transmit the packet to the routing component */
            if (action == FILTER_ACT_CONNECT || action == FILTER_ACT_ESTABLISHED || action == FILTER_ACT_ALLOW) {
                err = firewall_enqueue(&router_queue, net_firewall_desc(buffer));
                assert(!err);
                transmitted = true;

                if (FIREWALL_DEBUG_OUTPUT) {
                    if (action == FILTER_ACT_ALLOW || action == FILTER_ACT_CONNECT) {
                        sddf_printf("MAC[5] = %x | ICMP filter transmitting via rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                            filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT, ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT);
                    } else if (action == FILTER_ACT_ESTABLISHED) {
                        sddf_printf("MAC[5] = %x | ICMP filter transmitting via external rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                            filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT, ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT);
                    }
                }
            } else if (action == FILTER_ACT_DROP) {
                /* Return the buffer to the rx virtualiser */
                err = net_enqueue_free(&rx_queue, buffer);
                assert(!err);
                returned = true;

                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | ICMP filter dropping via rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                        filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, ICMP_FILTER_DUMMY_PORT, ip_pkt->dst_ip, ICMP_FILTER_DUMMY_PORT);
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

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    switch (microkit_msginfo_get_label(msginfo)) {
    case FIREWALL_SET_DEFAULT_ACTION: {
        firewall_action_t action = seL4_GetMR(FILTER_ARG_ACTION);
        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | ICMP filter changing default action from %u to %u\n",
                filter_config.mac_addr[5], filter_state.default_action, action);
        }
        firewall_filter_error_t err = firewall_filter_update_default_action(&filter_state, action);
        assert(err == FILTER_ERR_OKAY);

        seL4_SetMR(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FIREWALL_ADD_RULE: {
        firewall_action_t action = seL4_GetMR(FILTER_ARG_ACTION);
        uint32_t src_ip = seL4_GetMR(FILTER_ARG_SRC_IP);
        uint32_t dst_ip = seL4_GetMR(FILTER_ARG_DST_IP);
        uint8_t src_subnet = seL4_GetMR(FILTER_ARG_SRC_SUBNET);
        uint8_t dst_subnet = seL4_GetMR(FILTER_ARG_DST_SUBNET);
        uint16_t rule_id = 0;
        firewall_filter_error_t err = firewall_filter_add_rule(&filter_state, src_ip, ICMP_FILTER_DUMMY_PORT,
            dst_ip, ICMP_FILTER_DUMMY_PORT, src_subnet, dst_subnet, false, false, action, &rule_id);
        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | ICMP filter created rule %u with return code %u: (ip %u, mask %u, port %u, any_port %u) -(action %u)-> (ip %u, mask %u, port %u, any_port %u)\n",
                filter_config.mac_addr[5], rule_id, err, src_ip, src_subnet, ICMP_FILTER_DUMMY_PORT, false, action, dst_ip, dst_subnet, ICMP_FILTER_DUMMY_PORT, false);
        }
        seL4_SetMR(FILTER_RET_ERR, err);
        seL4_SetMR(FILTER_RET_RULE_ID, rule_id);
        return microkit_msginfo_new(0, 2);
    }
    case FIREWALL_DEL_RULE: {
        uint8_t rule_id = seL4_GetMR(FILTER_ARG_RULE_ID);
        firewall_filter_error_t err = firewall_filter_remove_rule(&filter_state, rule_id);
        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | ICMP removed rule id %u with return code %u\n",
                filter_config.mac_addr[5], rule_id, err);
        }
        seL4_SetMR(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        sddf_printf("ICMP_FILTER|LOG: unkown request %lu on channel %u\n",
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
        sddf_dprintf("ICMP_FILTER|LOG: Received notification on unknown channel: %d!\n", ch);
    }
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
        net_config.rx.num_buffers);
    
    firewall_queue_init(&router_queue, filter_config.router.queue.vaddr, filter_config.router.capacity);

    firewall_filter_state_init(&filter_state, filter_config.webserver.rules.vaddr, filter_config.rules_capacity,
        filter_config.internal_instances.vaddr, filter_config.external_instances.vaddr, filter_config.instances_capacity,
        (firewall_action_t)filter_config.webserver.default_action);
}
