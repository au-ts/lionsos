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
#include <lions/firewall/checksum.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/udp.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".fw_filter_config"))) fw_filter_config_t filter_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

/* Queues for receiving and transmitting packets */
net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;
fw_queue_t router_queue;

/* Holds filtering rules and state */
fw_filter_state_t filter_state;

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

            void *pkt_vaddr = net_config.rx_data.vaddr + buffer.io_or_offset;
            ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
            udp_hdr_t *udp_hdr = (udp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));

            uint16_t rule_id = 0;
            fw_action_t action = fw_filter_find_action(&filter_state, ip_hdr->src_ip, udp_hdr->src_port,
                                                                   ip_hdr->dst_ip, udp_hdr->dst_port, &rule_id);

            /* Add an established connection in shared memory for corresponding filter */
            if (action == FILTER_ACT_CONNECT) {
                fw_filter_err_t fw_err = fw_filter_add_instance(&filter_state, ip_pkt->src_ip, udp_hdr->src_port,
                                                                                ip_pkt->dst_ip, udp_hdr->dst_port, rule_id);

                if ((fw_err == FILTER_ERR_OKAY || fw_err == FILTER_ERR_DUPLICATE) && FW_DEBUG_OUTPUT) {
                    sddf_printf("%sUDP filter establishing connection via rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                        fw_frmt_str[filter_config.interface], rule_id,
                        ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0), htons(udp_hdr->src_port),
                        ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(udp_hdr->dst_port));
                }

                if (fw_err == FILTER_ERR_FULL) {
                    sddf_printf("%sUDP FILTER LOG: could not establish connection for rule %u: (ip %s, port %u) -> (ip %s, port %u): %s\n",
                        fw_frmt_str[filter_config.interface],
                        rule_id, ipaddr_to_string(ip_pkt->src_ip, ip_addr_buf0), HTONS(udp_hdr->src_port),
                        ipaddr_to_string(ip_pkt->dst_ip, ip_addr_buf1), HTONS(udp_hdr->dst_port), fw_filter_err_str[fw_err]);
                }
            }

            /* Transmit the packet to the routing component */
            if (action == FILTER_ACT_CONNECT || action == FILTER_ACT_ESTABLISHED || action == FILTER_ACT_ALLOW) {

                /* Reset the checksum as it's recalculated in hardware */
                #ifdef NETWORK_HW_HAS_CHECKSUM
                udp_hdr->check = 0;
                #endif
                err = fw_enqueue(&router_queue, &buffer);
                assert(!err);
                transmitted = true;

                if (FW_DEBUG_OUTPUT) {
                    if (action == FILTER_ACT_ALLOW || action == FILTER_ACT_CONNECT) {
                        sddf_printf("%sUDP filter transmitting via rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                            fw_frmt_str[filter_config.interface], rule_id,
                            ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0), htons(udp_hdr->src_port),
                            ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(udp_hdr->dst_port));
                    } else if (action == FILTER_ACT_ESTABLISHED) {
                        sddf_printf("%sUDP filter transmitting via external rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                            fw_frmt_str[filter_config.interface], rule_id,
                            ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0), htons(udp_hdr->src_port),
                            ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(udp_hdr->dst_port));
                    }
                }
            } else if (action == FILTER_ACT_DROP) {
                /* Return the buffer to the rx virtualiser */
                err = net_enqueue_free(&rx_queue, buffer);
                assert(!err);
                returned = true;

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("%sUDP filter dropping via rule %u: (ip %s, port %u) -> (ip %s, port %u)\n",
                        fw_frmt_str[filter_config.interface], rule_id,
                        ipaddr_to_string(ip_hdr->src_ip, ip_addr_buf0), htons(udp_hdr->src_port),
                        ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf1), htons(udp_hdr->dst_port));
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
    case FW_SET_DEFAULT_ACTION: {
        fw_action_t action = seL4_GetMR(FILTER_ARG_ACTION);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("%sUDP filter changing default action from %u to %u\n",
                fw_frmt_str[filter_config.interface], filter_state.rule_table->rules->action, action);
        }

        fw_filter_err_t err = fw_filter_update_default_action(&filter_state, action);
        assert(err == FILTER_ERR_OKAY);

        seL4_SetMR(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FW_ADD_RULE: {
        fw_action_t action = seL4_GetMR(FILTER_ARG_ACTION);
        uint32_t src_ip = seL4_GetMR(FILTER_ARG_SRC_IP);
        uint16_t src_port = seL4_GetMR(FILTER_ARG_SRC_PORT);
        uint32_t dst_ip = seL4_GetMR(FILTER_ARG_DST_IP);
        uint16_t dst_port = seL4_GetMR(FILTER_ARG_DST_PORT);
        uint8_t src_subnet = seL4_GetMR(FILTER_ARG_SRC_SUBNET);
        uint8_t dst_subnet = seL4_GetMR(FILTER_ARG_DST_SUBNET);
        bool src_port_any = seL4_GetMR(FILTER_ARG_SRC_ANY_PORT);
        bool dst_port_any = seL4_GetMR(FILTER_ARG_DST_ANY_PORT);
        uint16_t rule_id = 0;
        fw_filter_err_t err = fw_filter_add_rule(&filter_state, src_ip, src_port,
            dst_ip, dst_port, src_subnet, dst_subnet, src_port_any, dst_port_any, action, &rule_id);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("%sUDP filter create rule %u: (ip %s, mask %u, port %u, any_port %u) - (%s) -> (ip %s, mask %u, port %u, any_port %u): %s\n",
                fw_frmt_str[filter_config.interface], rule_id,
                ipaddr_to_string(src_ip, ip_addr_buf0), src_subnet, htons(src_port), src_port_any, fw_filter_action_str[action],
                ipaddr_to_string(dst_ip, ip_addr_buf1), dst_subnet, htons(dst_port), dst_port_any, fw_filter_err_str[err]);
        }

        seL4_SetMR(FILTER_RET_ERR, err);
        seL4_SetMR(FILTER_RET_RULE_ID, rule_id);
        return microkit_msginfo_new(0, 2);
    }
    case FW_DEL_RULE: {
        uint16_t rule_id = seL4_GetMR(FILTER_ARG_RULE_ID);
        fw_filter_err_t err = fw_filter_remove_rule(&filter_state, rule_id);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("%sUDP remove rule id %u: %s\n",
                fw_frmt_str[filter_config.interface], rule_id, fw_filter_err_str[err]);
        }

        seL4_SetMR(FILTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        sddf_printf("%sUDP FILTER LOG: unknown request %lu on channel %u\n",
            fw_frmt_str[filter_config.interface],
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
        sddf_dprintf("%sUDP FILTER LOG: Received notification on unknown channel: %d!\n",
            fw_frmt_str[filter_config.interface], ch);
    }
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
        net_config.rx.num_buffers);

    fw_queue_init(&router_queue, filter_config.router.queue.vaddr,
        sizeof(net_buff_desc_t),  filter_config.router.capacity);

    fw_filter_state_init(&filter_state, filter_config.webserver.rules.vaddr, filter_config.rule_id_bitmap.vaddr, filter_config.webserver.rules_capacity,
        filter_config.internal_instances.vaddr, filter_config.external_instances.vaddr, filter_config.instances_capacity,
        (fw_action_t)filter_config.webserver.default_action);
}
