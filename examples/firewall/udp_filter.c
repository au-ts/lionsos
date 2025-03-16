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
            udphdr_t *udp_hdr = (udphdr_t *)(pkt_vaddr + transport_layer_offset(ip_pkt));

            uint8_t rule_id = 0;
            firewall_action_t action = firewall_filter_find_action(&filter_state, ip_pkt->src_ip, udp_hdr->src_port,
                                                                   ip_pkt->dst_ip, udp_hdr->dst_port, &rule_id);

            /* Perform the default action */
            if (action == NONE) {
                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | UDP filter found no match, performing default action: (ip %u, port %u) -> (ip %u, port %u)\n",
                        filter_config.mac_addr[5], ip_pkt->src_ip, udp_hdr->src_port, ip_pkt->dst_ip, udp_hdr->dst_port);
                }
                action = filter_state.default_action;
            }
            
            /* Add an established connection in shared memory for corresponding filter */
            if (action == CONNECT) {
                firewall_filter_error_t fw_err = firewall_filter_add_instance(&filter_state, ip_pkt->src_ip, udp_hdr->src_port,
                                                                                ip_pkt->dst_ip, udp_hdr->dst_port, rule_id);

                if ((fw_err == OKAY || fw_err == DUPLICATE) && FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | UDP filter establishing connection via rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                        filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, udp_hdr->src_port, ip_pkt->dst_ip, udp_hdr->dst_port);
                } else if (fw_err == FULL) {
                    sddf_printf("UDP_FILTER|LOG: could not establish connection (full) for rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                        rule_id, ip_pkt->src_ip, udp_hdr->src_port, ip_pkt->dst_ip, udp_hdr->dst_port);
                }
            }

            /* Transmit the packet to the routing component */
            if (action == CONNECT || action == ESTABLISHED || action == ALLOW) {
                err = firewall_enqueue(&router_queue, net_firewall_desc(buffer));
                assert(!err);
                transmitted = true;

                if (FIREWALL_DEBUG_OUTPUT) {
                    if (action == ALLOW || action == CONNECT) {
                        sddf_printf("MAC[5] = %x | UDP filter transmitting via rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                            filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, udp_hdr->src_port, ip_pkt->dst_ip, udp_hdr->dst_port);
                    } else if (action == ESTABLISHED) {
                        sddf_printf("MAC[5] = %x | UDP filter transmitting via external rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                            filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, udp_hdr->src_port, ip_pkt->dst_ip, udp_hdr->dst_port);
                    }
                }
            } else if (action == DROP) {
                /* Return the buffer to the rx virtualiser */
                err = net_enqueue_free(&rx_queue, buffer);
                assert(!err);
                returned = true;

                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | UDP filter dropping via rule %u: (ip %u, port %u) -> (ip %u, port %u)\n",
                        filter_config.mac_addr[5], rule_id, ip_pkt->src_ip, udp_hdr->src_port, ip_pkt->dst_ip, udp_hdr->dst_port);
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
        firewall_action_t action = seL4_GetMR(ACTION);
        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | UDP filter changing default action from %u to %u\n",
                filter_config.mac_addr[5], filter_state.default_action, action);
        }
        filter_state.default_action = action;
        seL4_SetMR(0, OKAY);
        return microkit_msginfo_new(0, 1);
    }
    case FIREWALL_ADD_RULE: {
        firewall_action_t action = seL4_GetMR(ACTION);
        uint32_t src_ip = seL4_GetMR(SRC_IP);
        uint16_t src_port = seL4_GetMR(SRC_PORT);
        uint32_t dst_ip = seL4_GetMR(DST_IP);
        uint16_t dst_port = seL4_GetMR(DST_PORT);
        uint8_t src_subnet = seL4_GetMR(SRC_SUBNET);
        uint8_t dst_subnet = seL4_GetMR(DST_SUBNET);
        bool src_port_any = seL4_GetMR(SRC_ANY_PORT);
        bool dst_port_any = seL4_GetMR(DST_ANY_PORT);
        uint8_t rule_id = 0;
        firewall_filter_error_t err = firewall_filter_add_rule(&filter_state, src_ip, src_port,
            dst_ip, dst_port, src_subnet, dst_subnet, src_port_any, dst_port_any, action, &rule_id);
        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | UDP filter created rule %u with return code %u: (ip %u, mask %u, port %u, any_port %u) -(action %u)-> (ip %u, mask %u, port %u, any_port %u)\n",
                filter_config.mac_addr[5], rule_id, err, src_ip, src_subnet, src_port, src_port_any, action, dst_ip, dst_subnet, dst_port, dst_port_any);
        }
        seL4_SetMR(0, err);
        seL4_SetMR(1, rule_id);
        return microkit_msginfo_new(0, 2);
    }
    case FIREWALL_DEL_RULE: {
        uint8_t rule_id = seL4_GetMR(RULE_ID);
        firewall_filter_error_t err = firewall_filter_remove_rule(&filter_state, rule_id);
        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | UDP removed rule id %u with return code %u\n",
                filter_config.mac_addr[5], rule_id, err);
        }
        seL4_SetMR(0, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        sddf_printf("UDP_FILTER|LOG: unkown request %lu on channel %u\n",
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
        sddf_dprintf("UDP_FILTER|LOG: Received notification on unknown channel: %d!\n", ch);
    }
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
        net_config.rx.num_buffers);
    
    firewall_queue_init(&router_queue, filter_config.router.queue.vaddr, filter_config.router.capacity);

    firewall_filter_state_init(&filter_state, filter_config.webserver.rules.vaddr,
        filter_config.internal_instances.vaddr, filter_config.external_instances.vaddr, (firewall_action_t)filter_config.webserver.default_action);
}
