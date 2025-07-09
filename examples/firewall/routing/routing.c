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
#include <sddf/network/util.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/firewall/arp_queue.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/routing.h>
#include <lions/firewall/common.h>
#include <lions/firewall/icmp_queue.h>
#include <string.h>

#define SECTION(sect) __attribute__((__section__(sect)))
SECTION(".serial_client_config") serial_client_config_t serial_config;
SECTION(".fw_router_config") fw_router_config_t router_config;

/* Port that the webserver is on. */
#define WEBSERVER_PROTOCOL 0x06
#define WEBSERVER_PORT 80

serial_queue_handle_t serial_tx_queue_handle;

/* DMA buffer data structures */
fw_queue_handle_t fw_filters[FW_MAX_FILTERS]; /* Filter queues to
                                               * receive packets */
fw_queue_handle_t rx_free; /* Queue to return free rx buffers */
fw_queue_handle_t tx_active; /* Queue to transmit packets out the network */
fw_queue_handle_t webserver; /* Queue to route to webserver */
uintptr_t data_vaddr; /* Virtual address or rx buffer data region */
icmp_queue_handle_t icmp_queue; /* Queue to transmit ICMP requests to
                                 * the ICMP module. */

/* Arp request/entry data structures */
fw_arp_queue_handle_t *arp_queue; /* This queue holds ARP
                                   * requests/responses for the arp
                                   * requester */
fw_arp_table_t arp_table; /* ARP table holding all known ARP entries */
pkts_waiting_t pkt_waiting_queue; /* Queue holding packets awaiting
                                   * arp responses */

/* Routing data structures */
fw_routing_table_t *routing_table; /* Table holding next hop data for subnets */

/* Booleans to keep track of which components need to be notified */
static bool tx_net; /* Packet has been transmitted to the network tx
                     * virtualiser */
static bool tx_webserver; /* Packet has been transmitted to the webserver */
static bool returned; /* Buffer has been returned to the rx virtualiser */
static bool notify_arp; /* Arp request has been enqueued */
static bool notify_icmp; /* Request has been enqueued to ICMP module */

static void process_arp_waiting(void)
{

    while (!fw_arp_queue_empty_response(arp_queue)) {
        fw_arp_request_t response; 
        pkt_waiting_node_t *req_pkt;
        fw_routing_err_t routing_err;
        int err = fw_arp_dequeue_response(arp_queue, &response);
        assert(!err);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("%sRouter dequeuing response for ip %s and MAC[0] = %x, MAC[5] = %x\n",
                fw_frmt_str[router_config.webserver.interface],
                ipaddr_to_string(response.ip, ip_addr_buf0),
                        response.mac_addr[0], response.mac_addr[5]);
        }

        /* Check that we actually have a packet waiting. */
        req_pkt = pkt_waiting_find_node(&pkt_waiting_queue, response.ip);
        if (!req_pkt) {
            continue;
        }

        /* Send or drop all matching ip packets */
        if (response.state == ARP_STATE_UNREACHABLE) {
            /* Invalid response, drop packet associated with the IP address */
            pkt_waiting_node_t *pkt_node = req_pkt;
            for (uint16_t i = 0; i < req_pkt->num_children; i++) {
                ipv4_packet_t *eth_hdr;
                icmp_req_t req = {0};
                assert(&icmp_queue != NULL);
                req.ip = pkt_node->ip;
                eth_hdr = (ipv4_packet_t *)(data_vaddr +
                                            pkt_node->buffer.io_or_offset);
                /*
                 * Copy the source of the failed packet as the dest of
                 * the ICMP response.
                 */
                memcpy(req.mac, eth_hdr->ethsrc_addr, ETH_HWADDR_LEN);
                req.type = ICMP_DEST_UNREACHABLE;
                req.code = ICMP_DEST_HOST_UNREACHABLE;
                memcpy(&req.old_hdr, eth_hdr, sizeof(ipv4_packet_t));
                if (pkt_node->buffer.len >= (sizeof(ipv4_packet_t) + 8)) {
                    memcpy(&req.old_data,
                           (void *)(pkt_node->buffer.io_or_offset
                                    + data_vaddr + sizeof(ipv4_packet_t)),
                           8);
                }
                err = icmp_enqueue(&icmp_queue, req);
                if (err) {
                    sddf_dprintf("%s| ICMP queue was full.", microkit_name);
                }
                notify_icmp = true;
                err = fw_enqueue(&rx_free, pkt_node->buffer);
                assert(!err);
                pkt_node = pkts_waiting_next_child(&pkt_waiting_queue, pkt_node);
            }
        } else {
            /* Substitute the MAC address and send packets out of the NIC */
            pkt_waiting_node_t *pkt_node = req_pkt;
            for (uint16_t i = 0; i < req_pkt->num_children + 1; i++) {
                ipv4_packet_t *tx_pkt;
                tx_pkt = (ipv4_packet_t *)(data_vaddr +
                                           pkt_node->buffer.io_or_offset);
                memcpy(tx_pkt->ethdst_addr, response.mac_addr, ETH_HWADDR_LEN);
                memcpy(tx_pkt->ethsrc_addr, router_config.mac_addr,
                       ETH_HWADDR_LEN);
                tx_pkt->check = 0;

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("%sRouter sending packet for ip %s (next hop %s) with buffer number %lu\n",
                        fw_frmt_str[router_config.webserver.interface],
                        ipaddr_to_string(tx_pkt->dst_ip, ip_addr_buf0),
                                ipaddr_to_string(response.ip, ip_addr_buf1),
                        pkt_node->buffer.io_or_offset/NET_BUFFER_SIZE);
                }

                err = fw_enqueue(&tx_active, pkt_node->buffer);
                assert(!err);
                tx_net = true;
                pkt_node = pkts_waiting_next_child(&pkt_waiting_queue, pkt_node);
            }
        }
        /* Free the packet waiting nodes */
        routing_err = pkts_waiting_free_parent(&pkt_waiting_queue, req_pkt);
        assert(routing_err == ROUTING_ERR_OKAY);
    }
}

static void route()
{
    for (int filter = 0; filter < router_config.num_filters; filter++) {
        while (!fw_queue_empty(&fw_filters[filter])) {
            fw_buff_desc_t buffer;
            uint32_t next_hop;
            fw_routing_interfaces_t interface;
            fw_routing_err_t fw_err;
            int err = fw_dequeue(&fw_filters[filter], &buffer);
            assert(!err);

            uintptr_t pkt_vaddr = data_vaddr + buffer.io_or_offset;
            ipv4_packet_t *ip_pkt = (ipv4_packet_t *)(pkt_vaddr);

            /*
             * Decrement the TTL field. If it reaches 0 protocol is
             * that we drop the packet in this router.
             *
             * NOTE: We drop non-IPv4 packets. This case should be
             * handled by the protocol virtualiser.
             */
            if (ip_pkt->ttl > 1 && ip_pkt->type == HTONS(ETH_TYPE_IP)) {
                ip_pkt->ttl -= 1;

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("%sRouter received packet for ip %s with buffer number %lu\n",
                        fw_frmt_str[router_config.webserver.interface],
                        ipaddr_to_string(ip_pkt->dst_ip, ip_addr_buf0),
                                buffer.io_or_offset/NET_BUFFER_SIZE);
                }

                /* Find the next hop address. */
                fw_err = fw_routing_find_route(routing_table,
                                               ip_pkt->dst_ip,
                                               &next_hop,
                                               &interface,
                                               0);
                assert(fw_err == ROUTING_ERR_OKAY);

                if (FW_DEBUG_OUTPUT && interface != ROUTING_OUT_NONE) {
                    sddf_printf("%sRouter converted ip %s to next hop ip %s out interface %u\n",
                        fw_frmt_str[router_config.webserver.interface],
                        ipaddr_to_string(ip_pkt->dst_ip, ip_addr_buf0),
                        ipaddr_to_string(next_hop, ip_addr_buf1), interface);
                }

                /* No route, drop packet  */
                if (interface == ROUTING_OUT_NONE ||
                    (router_config.interface == FW_EXTERNAL_INTERFACE_ID &&
                    interface == ROUTING_OUT_INTERNAL)) {

                    if (FW_DEBUG_OUTPUT) {
                        sddf_printf("%sRouter found no route for ip %s, dropping packet\n",
                            ipaddr_to_string(ip_pkt->dst_ip, ip_addr_buf0),
                            fw_frmt_str[router_config.webserver.interface]);
                    }

                    err = fw_enqueue(&rx_free, buffer);
                    assert(!err);
                    returned = true;
                    continue;
                } else if (router_config.interface == FW_INTERNAL_INTERFACE_ID
                           &&
                           interface == ROUTING_OUT_INTERNAL) {
                    tcphdr_t *tcp_pkt;
                    tcp_pkt = (tcphdr_t *)(pkt_vaddr +
                                           transport_layer_offset(ip_pkt));

                    /* Webserver only accepts TCP traffic on webserver port */
                    if (ip_pkt->protocol != WEBSERVER_PROTOCOL
                        || tcp_pkt->dst_port != HTONS(WEBSERVER_PORT)) {
                        err = fw_enqueue(&rx_free, buffer);
                        assert(!err);
                        returned = true;
                        continue;
                    }

                    /* Forward packet to the webserver */
                    err = fw_enqueue(&webserver, buffer);
                    assert(!err);
                    tx_webserver = true;

                    if (FW_DEBUG_OUTPUT) {
                        sddf_printf("%sRouter transmitted packet to webserver\n",
                        fw_frmt_str[router_config.webserver.interface]);
                    }

                } else {
                    fw_arp_entry_t *arp;
                    arp = fw_arp_table_find_entry(&arp_table, next_hop);
                    if (arp == NULL ||
                        arp->state == ARP_STATE_PENDING ||
                        arp->state == ARP_STATE_UNREACHABLE) {
                        if ((arp != NULL &&
                             arp->state == ARP_STATE_UNREACHABLE) ||
                            pkt_waiting_full(&pkt_waiting_queue)) {
                            sddf_dprintf("%sROUTING LOG: Waiting packet queue full or destination unreachable, dropping packet!\n",
                                fw_frmt_str[router_config.webserver.interface]);
                            // Enqueuing request to the ICMP module to send a destintion unreachable packet back to the source
                            icmp_req_t req = {0};
                            req.ip = ip_pkt->dst_ip;
                            // Copy the source of the failed packet as
                            // the dest of the ICMP response.
                            memcpy(req.mac, ip_pkt->ethsrc_addr, ETH_HWADDR_LEN);
                            req.type = ICMP_DEST_UNREACHABLE;
                            req.code = ICMP_DEST_HOST_UNREACHABLE;
                            memcpy(&req.old_hdr, ip_pkt, sizeof(ipv4_packet_t));
                            if (buffer.len >= (sizeof(ipv4_packet_t) + 8)) {
                                sddf_memcpy(&req.old_data,
                                            (void *)(buffer.io_or_offset +
                                                     data_vaddr +
                                                     sizeof(ipv4_packet_t)), 8);
                            }
                            err = icmp_enqueue(&icmp_queue, req);
                            if (err) {
                                sddf_dprintf("%s| ICMP queue was full.", microkit_name);
                            }
                            notify_icmp = true;
                            err = fw_enqueue(&rx_free, buffer);
                            assert(!err);
                            returned = true;
                        } else {
                            /*
                             * In this case, the IP address is not in
                             * the ARP Tables.  We add an entry to the
                             * ARP request queue and await a
                             * response. If we get a timeout, we will
                             * then drop the packets associated with
                             * that IP address in the queue.
                             */
                            pkt_waiting_node_t *parent;
                            parent = pkt_waiting_find_node(&pkt_waiting_queue,
                                                           next_hop);
                            if (parent) {
                                /* ARP request already enqueued, add node as child. */
                                fw_err = pkt_waiting_push_child(
                                    &pkt_waiting_queue,
                                    parent,
                                    next_hop,
                                    buffer);
                                assert(fw_err == ROUTING_ERR_OKAY);
                            } else if (fw_arp_queue_full_request(arp_queue)) {
                                /*
                                 * No existing ARP request and queue
                                 * is full, drop packet.
                                 */
                                sddf_dprintf("%sROUTING LOG: ARP request queue full, dropping packet!\n",
                                    fw_frmt_str[router_config.webserver.interface]);
                                err = fw_enqueue(&rx_free, buffer);
                                assert(!err);
                                returned = true;
                            } else {
                                /* Generate ARP request and enqueue packet. */
                                fw_arp_request_t request = {
                                    next_hop,
                                    {0},
                                    ARP_STATE_INVALID
                                };
                                err = fw_arp_enqueue_request(arp_queue, request);
                                assert(!err);
                                fw_err = pkt_waiting_push(&pkt_waiting_queue,
                                                          next_hop,
                                                          buffer);
                                assert(fw_err == ROUTING_ERR_OKAY);
                                notify_arp = true;
                            }
                        }
                    } else {
                        /*
                         * Match found for MAC address, replace the
                         * destination in eth header
                         */
                        memcpy(&ip_pkt->ethdst_addr, &arp->mac_addr,
                               ETH_HWADDR_LEN);
                        memcpy(&ip_pkt->ethsrc_addr, router_config.mac_addr,
                               ETH_HWADDR_LEN);
                        ip_pkt->check = 0;

                        /* Transmit packet out the NIC */
                        if (FW_DEBUG_OUTPUT) {
                            sddf_printf("%sRouter sending packet for ip %s (next hop %s) with buffer number %lu\n",
                                fw_frmt_str[router_config.webserver.interface],
                                ipaddr_to_string(ip_pkt->dst_ip, ip_addr_buf0),
                                        ipaddr_to_string(next_hop, ip_addr_buf1),
                                buffer.io_or_offset/NET_BUFFER_SIZE);
                        }

                        int err = fw_enqueue(&tx_active, buffer);
                        assert(!err);
                        tx_net = true;
                    }
                }
            }
        }
    }
}

void init(void)
{
    serial_queue_init(&serial_tx_queue_handle,
                      serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    /* Set up firewall filter queues */
    for (int i = 0; i < router_config.num_filters; i++) {
        fw_queue_init(&fw_filters[i], router_config.filters[i].queue.vaddr,
                            router_config.filters[i].capacity);
    }

    /* Set up virt rx firewall queue */
    fw_queue_init(&rx_free, router_config.rx_free.queue.vaddr,
                        router_config.rx_free.capacity);

    /* Set up virt tx firewall queue */
    fw_queue_init(&tx_active, router_config.tx_active.queue.vaddr,
        router_config.tx_active.capacity);

    data_vaddr = (uintptr_t)router_config.data.vaddr;

    /* Initialise arp queues */
    arp_queue = (fw_arp_queue_handle_t *) router_config.arp_queue.queue.vaddr;
    fw_arp_handle_init(arp_queue, router_config.arp_queue.capacity);
    fw_arp_table_init(&arp_table, (fw_arp_entry_t *)router_config.arp_cache.vaddr, router_config.arp_cache_capacity);

    icmp_queue_init(&icmp_queue, router_config.icmp_module.queue.vaddr,
                    router_config.icmp_module.capacity);

    /* Initialise routing table */
    fw_routing_table_init(&routing_table,
                          router_config.webserver.routing_table.vaddr,
                          router_config.webserver.routing_table_capacity,
                          router_config.out_ip,
                          router_config.out_subnet);

    /* Set up router --> webserver queue. */
    if (router_config.interface == FW_INTERNAL_INTERFACE_ID) {
        fw_queue_init(&webserver, router_config.rx_active.queue.vaddr,
                            router_config.rx_active.capacity);
        
        /* Add an entry for the webserver */
        fw_routing_table_add_route(routing_table,
                                   ROUTING_OUT_INTERNAL,
                                   router_config.ip,
                                   32,
                                   router_config.ip);
    }

    assert(router_config.packet_queue.vaddr != 0);
    /* Initialise the packet waiting queue from mapped in memory */
    pkt_waiting_init(&pkt_waiting_queue,
                     (icmp_queue_t *) router_config.packet_queue.vaddr,
                     router_config.rx_free.capacity);
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    switch (microkit_msginfo_get_label(msginfo)) {
    case FW_ADD_ROUTE: {
        uint32_t ip = seL4_GetMR(ROUTER_ARG_IP);
        uint8_t subnet = seL4_GetMR(ROUTER_ARG_SUBNET);
        uint32_t next_hop = seL4_GetMR(ROUTER_ARG_NEXT_HOP);
        // @kwinter: Limiting this to just external routes out of the NIC
        // for now.
        fw_routing_err_t err = fw_routing_table_add_route(routing_table,
                                                          ROUTING_OUT_EXTERNAL,
                                                          ip,
                                                          subnet,
                                                          next_hop);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("%sRouter add route. (ip %s, mask %u, next hop %s): %s\n",
                fw_frmt_str[router_config.webserver.interface],
                ipaddr_to_string(ip, ip_addr_buf0), subnet,
                ipaddr_to_string(next_hop, ip_addr_buf1),
                        fw_routing_err_str[err]);
        }
        seL4_SetMR(ROUTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FW_DEL_ROUTE: {
        uint16_t route_id = seL4_GetMR(ROUTER_ARG_ROUTE_ID);
        fw_routing_err_t err = fw_routing_table_remove_route(routing_table, route_id);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("%sRouter delete route %u: %s\n",
                fw_frmt_str[router_config.webserver.interface],
                route_id, fw_routing_err_str[err]);
        }

        seL4_SetMR(ROUTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        sddf_printf("%sROUTING LOG: unknown request %lu on channel %u\n",
            fw_frmt_str[router_config.webserver.interface],
            microkit_msginfo_get_label(msginfo), ch);
        break;
    }

    return microkit_msginfo_new(0, 0);
}

void notified(microkit_channel ch)
{
    if (ch == router_config.arp_queue.ch) {
        /*
         * This is the channel between the ARP component and the
         * routing component
         */
        process_arp_waiting();
    } else {
        /* Router has been notified by a filter */
        route();
    }

    if (notify_icmp) {
        notify_icmp = false;
        microkit_notify(router_config.icmp_module.ch);
    }

    if (notify_arp) {
        notify_arp = false;
        microkit_notify(router_config.arp_queue.ch);
    }

    if (router_config.interface == FW_INTERNAL_INTERFACE_ID && tx_webserver) {
        tx_webserver = false;
        microkit_notify(router_config.rx_active.ch);
    }

    if (returned) {
        returned = false;
        microkit_deferred_notify(router_config.rx_free.ch);
    }

    if (tx_net) {
        tx_net = false;
        microkit_notify(router_config.tx_active.ch);
    }
}
