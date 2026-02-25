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
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/firewall/arp.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/filter.h>
#include <lions/firewall/icmp.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/routing.h>
#include <lions/firewall/tcp.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".fw_router_config"))) fw_router_config_t router_config;

/* Port that the webserver is on. */
#define WEBSERVER_PROTOCOL 0x06
#define WEBSERVER_PORT 80

serial_queue_handle_t serial_tx_queue_handle;

/* DMA buffer data structures */
fw_queue_t fw_filters[FW_MAX_INTERFACES][FW_MAX_FILTERS];   /* Filter queues to
                                                             * receive packets */
fw_queue_t rx_free[FW_MAX_INTERFACES];                      /* Queues to return free rx buffers */
fw_queue_t tx_active[FW_MAX_INTERFACES];                    /* Queues to transmit packets out interfaces */
fw_queue_t webserver;                                       /* Queue to route to webserver */
uintptr_t data_vaddr[FW_MAX_INTERFACES];                    /* Virtual address of rx buffer data regions */
fw_queue_t icmp_queue;                                      /* Queue to transmit ICMP requests to
                                                             * the ICMP module. */

/* Arp request/entry data structures */
fw_queue_t arp_req_queue[FW_MAX_INTERFACES];
fw_queue_t arp_resp_queue[FW_MAX_INTERFACES];
fw_arp_table_t arp_table[FW_MAX_INTERFACES]; /* ARP table holding all known ARP entries */
pkts_waiting_t pkt_waiting_queue;            /* Queue holding packets awaiting
                                              * arp responses */

/* Routing data structures */
fw_routing_table_t *routing_table; /* Table holding next hop data for subnets */

/* Booleans to keep track of which components need to be notified */
static bool tx_net[FW_MAX_INTERFACES];                    /* Packet has been transmitted to the
                                                           * network tx virtualiser */
static bool tx_webserver;                                 /* Packet has been transmitted to the webserver */
static bool returned[FW_MAX_INTERFACES];                  /* Buffer has been returned to the rx virtualiser */
static bool notify_arp[FW_MAX_INTERFACES];                /* Arp request has been enqueued */
static bool notify_icmp;                                  /* Request has been enqueued to ICMP module */

/* Enqueue a request to the ICMP module to transmit a destination unreachable
packet back to source */
static int enqueue_icmp_unreachable(net_buff_desc_t buffer, uint8_t src_interface)
{
    icmp_req_t req = { 0 };
    req.type = ICMP_DEST_UNREACHABLE;
    req.code = ICMP_DEST_HOST_UNREACHABLE;
    req.out_interface = src_interface;

    /* Copy ethernet header into ICMP request */
    uintptr_t pkt_vaddr = data_vaddr[src_interface] + buffer.io_or_offset;
    memcpy(&req.eth_hdr, (void *)pkt_vaddr, ETH_HDR_LEN);

    /* Copy IP header into ICMP request */
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);
    memcpy(&req.ip_hdr, (void *)ip_hdr, IPV4_HDR_LEN_MIN);

    /* Copy first bytes of data if applicable */
    uint16_t to_copy = MIN(FW_ICMP_SRC_DATA_LEN, htons(ip_hdr->tot_len) - IPV4_HDR_LEN_MIN);
    memcpy(req.data, (void *)(pkt_vaddr + IPV4_HDR_OFFSET + IPV4_HDR_LEN_MIN), to_copy);

    int err = fw_enqueue(&icmp_queue, &req);
    if (!err) {
        notify_icmp = true;
    }

    return err;
}

static void transmit_packet(net_buff_desc_t buffer, uint8_t *mac_addr, uint8_t src_interface,
                            uint8_t out_interface)
{
    uintptr_t pkt_vaddr = data_vaddr[src_interface] + buffer.io_or_offset;
    eth_hdr_t *eth_hdr = (eth_hdr_t *)pkt_vaddr;
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);

    memcpy(&eth_hdr->ethdst_addr, mac_addr, ETH_HWADDR_LEN);
    memcpy(&eth_hdr->ethsrc_addr, router_config.interfaces[out_interface].mac_addr, ETH_HWADDR_LEN);

    /* Transmit packet out the NIC */
    if (FW_DEBUG_OUTPUT) {
        sddf_printf(
            "Router sending packet received on interface %u out of interface %u for ip %s with buffer number %lu\n",
            src_interface, out_interface, ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0),
            buffer.io_or_offset / NET_BUFFER_SIZE);
    }

    /* Checksum needs to be re-calculated as header has been modified */
    ip_hdr->check = 0;
#ifndef NETWORK_HW_HAS_CHECKSUM
    /* Recalculate IP packet checksum in software */
    ip_hdr->check = fw_internet_checksum(ip_hdr, ipv4_header_length(ip_hdr));
#endif

    fw_buff_desc_t fw_buffer = { .offset = buffer.io_or_offset, .len = buffer.len, .interface = src_interface };
    int err = fw_enqueue(&tx_active[out_interface], &fw_buffer);
    assert(!err);
    tx_net[out_interface] = true;
}

static void process_arp_waiting(fw_interface_id_t out_interface)
{
    while (!fw_queue_empty(&arp_resp_queue[out_interface])) {
        fw_arp_request_t response;
        int err = fw_dequeue(&arp_resp_queue[out_interface], &response);
        assert(!err);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("Router dequeuing response for ip %s on interface %u and MAC[0]= %x, MAC[5] = %x\n",
                        ipaddr_to_string(response.ip, ip_addr_buf0), out_interface, response.mac_addr[0],
                        response.mac_addr[5]);
        }

        /* Check that we actually have a packet waiting. */
        pkt_waiting_node_t *root = pkt_waiting_find_node(&pkt_waiting_queue, response.ip);
        if (!root) {
            continue;
        }

        /* Send or drop all matching ip packets */
        if (response.state == ARP_STATE_UNREACHABLE) {
            /* Invalid response, drop packet associated with the IP address */
            pkt_waiting_node_t *node = root;
            for (uint16_t i = 0; i < root->num_children + 1; i++) {
                err = enqueue_icmp_unreachable(node->buffer, node->buffer.interface);
                if (FW_DEBUG_OUTPUT && err) {
                    sddf_printf("ROUTING LOG: Could not enqueue ICMP unreachable!\n");
                }
                err = fw_enqueue(&rx_free[node->buffer.interface], &node->buffer);
                returned[node->buffer.interface] = true;
                assert(!err);
                node = pkts_waiting_next_child(&pkt_waiting_queue, node);
            }
        } else {
            /* Substitute the MAC address and send packets out of the NIC */
            pkt_waiting_node_t *node = root;
            for (uint16_t i = 0; i < root->num_children + 1; i++) {
                transmit_packet(node->buffer, response.mac_addr, node->buffer.interface, out_interface);
                node = pkts_waiting_next_child(&pkt_waiting_queue, node);
            }
        }
        /* Free the packet waiting nodes */
        fw_routing_err_t routing_err = pkts_waiting_free_parent(&pkt_waiting_queue, root);
        assert(routing_err == ROUTING_ERR_OKAY);
    }
}

static void route(void)
{
    for (uint8_t interface = 0; interface < router_config.num_interfaces; interface++) {
        for (uint8_t filter = 0; filter < router_config.interfaces[interface].num_filters; filter++) {
            while (!fw_queue_empty(&fw_filters[interface][filter])) {
                net_buff_desc_t buffer;
                int err = fw_dequeue(&fw_filters[interface][filter], &buffer);
                assert(!err);

                uintptr_t pkt_vaddr = data_vaddr[interface] + buffer.io_or_offset;
                eth_hdr_t *eth_hdr = (eth_hdr_t *)pkt_vaddr;
                ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt_vaddr + IPV4_HDR_OFFSET);

                /*
                 * Decrement the TTL field. If it reaches 0 protocol is
                 * that we drop the packet in this router.
                 *
                 * NOTE: We drop non-IPv4 packets. This case should be
                 * handled by the protocol virtualiser.
                 */
                if (eth_hdr->ethtype != htons(ETH_TYPE_IP) || ip_hdr->ttl <= 1) {
                    err = fw_enqueue(&rx_free[interface], &buffer);
                    assert(!err);
                    returned[interface] = true;
                    continue;
                }

                ip_hdr->ttl -= 1;

                if (FW_DEBUG_OUTPUT) {
                    sddf_printf("Router received packet on interface %u for ip %s with buffer number %lu\n", interface,
                                ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0), buffer.io_or_offset / NET_BUFFER_SIZE);
                }

                /* Packet destined for webserver */
                if (ip_hdr->dst_ip == fw_config.interfaces[interface].ip) {
                    if (FW_DEBUG_OUTPUT) {
                        sddf_printf("Router transmitted packet to webserver on interface %u\n", interface);
                    }
                    tcp_hdr_t *tcp_pkt = (tcp_hdr_t *)(pkt_vaddr + transport_layer_offset(ip_hdr));
                    assert(ip_hdr->protocol == WEBSERVER_PROTOCOL && tcp_pkt->dst_port == htons(WEBSERVER_PORT));
                    err = fw_enqueue(&webserver, &buffer);
                    assert(!err);
                    tx_webserver = true;
                    continue;
                }

                uint32_t next_hop = ip_hdr->dst_ip;
                uint8_t out_interface;
                fw_routing_err_t fw_err = fw_routing_find_route(routing_table, &next_hop, &out_interface);
                assert(fw_err == ROUTING_ERR_OKAY);

                if (FW_DEBUG_OUTPUT && next_hop != FW_ROUTING_NONEXTHOP) {
                    sddf_printf("Router converted ip %s to next hop ip %s using out interface %u\n",
                                ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0),
                                ipaddr_to_string(next_hop, ip_addr_buf1), out_interface);
                }

                /* No route, drop packet  */
                if (next_hop == FW_ROUTING_NONEXTHOP) {
                    if (FW_DEBUG_OUTPUT) {
                        sddf_printf("Router found no route for ip %s, dropping packet\n",
                                    ipaddr_to_string(ip_hdr->dst_ip, ip_addr_buf0));
                    }
                    err = fw_enqueue(&rx_free[interface], &buffer);
                    assert(!err);
                    returned[interface] = true;
                    continue;
                }

                assert(out_interface < router_config.num_interfaces);

                fw_arp_entry_t *arp = fw_arp_table_find_entry(&arp_table[out_interface], next_hop);
                /* destination unreachable or no space to store packet or send ARP
                 * request, drop packet */
                if ((arp != NULL && arp->state == ARP_STATE_UNREACHABLE)
                    || (pkt_waiting_full(&pkt_waiting_queue) && (arp == NULL || arp->state == ARP_STATE_PENDING))
                    || (arp == NULL && fw_queue_full(&arp_req_queue[out_interface]))) {

                    if (arp != NULL && arp->state == ARP_STATE_UNREACHABLE) {
                        int icmp_err = enqueue_icmp_unreachable(buffer, interface);
                        if (icmp_err) {
                            sddf_dprintf("ROUTING LOG: Could not enqueue ICMP unreachable!\n");
                        }
                    } else {
                        sddf_dprintf("ROUTING LOG: Waiting packet or ARP request queue full, dropping packet!\n");
                    }

                    err = fw_enqueue(&rx_free[interface], &buffer);
                    assert(!err);
                    returned[interface] = true;
                    continue;
                }

                /* no entry in ARP table or request still pending, store packet
                and send ARP request or await ARP response */
                if (arp == NULL || arp->state == ARP_STATE_PENDING) {
                    pkt_waiting_node_t *root = pkt_waiting_find_node(&pkt_waiting_queue, next_hop);
                    fw_buff_desc_t fw_buffer = { .io_or_offset = buffer.io_or_offset, .len = buffer.len, .interface = interface };
                    if (root) {
                        /* ARP request already enqueued, add node as child. */
                        fw_err = pkt_waiting_push_child(&pkt_waiting_queue, root, fw_buffer);
                        assert(fw_err == ROUTING_ERR_OKAY);
                    } else {
                        /* Generate ARP request and enqueue packet. */
                        fw_arp_request_t request = { next_hop, { 0 }, ARP_STATE_INVALID };
                        err = fw_enqueue(&arp_req_queue[out_interface], &request);
                        assert(!err);
                        fw_err = pkt_waiting_push(&pkt_waiting_queue, next_hop, fw_buffer);
                        assert(fw_err == ROUTING_ERR_OKAY);
                        notify_arp[out_interface] = true;
                    }

                    continue;
                }

                /* valid arp entry found, transmit packet */
                transmit_packet(buffer, arp->mac_addr, interface, out_interface);
            }
        }
    }
}

void init(void)
{
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    for (uint8_t interface = 0; interface < router_config.num_interfaces; interface++) {
        fw_router_interface_t *iface = &router_config.interfaces[interface];
        /* Set up firewall filter queues */
        for (int f = 0; f < iface->num_filters; f++) {
            fw_queue_init(&fw_filters[interface][f], iface->filters[f].queue.vaddr, sizeof(net_buff_desc_t),
                          iface->filters[f].capacity);
        }

        /* Set up virt rx firewall queue */
        fw_queue_init(&rx_free[interface], iface->rx_free.queue.vaddr, sizeof(net_buff_desc_t),
                      iface->rx_free.capacity);

        /* Set up virt tx firewall queue */
        fw_queue_init(&tx_active[interface], iface->tx_active.queue.vaddr, sizeof(fw_buff_desc_t),
                      iface->tx_active.capacity);

        data_vaddr[interface] = (uintptr_t)iface->data.vaddr;

        /* Initialise arp queues */
        fw_queue_init(&arp_req_queue[interface], iface->arp_queue.request.vaddr, sizeof(fw_arp_request_t),
                      iface->arp_queue.capacity);
        fw_queue_init(&arp_resp_queue[interface], iface->arp_queue.response.vaddr, sizeof(fw_arp_request_t),
                      iface->arp_queue.capacity);
        fw_arp_table_init(&arp_table[interface], (fw_arp_entry_t *)iface->arp_cache.vaddr, iface->arp_cache_capacity);
    }

    fw_queue_init(&icmp_queue, router_config.icmp_module.queue.vaddr, sizeof(icmp_req_t),
                  router_config.icmp_module.capacity);

    /* Initialise routing table */
    fw_routing_table_init(&routing_table, router_config.webserver.routing_table.vaddr,
                          router_config.webserver.routing_table_capacity, router_config.initial_routes,
                          router_config.num_initial_routes);

    if (FW_DEBUG_OUTPUT) {
        sddf_printf("Routing table initialized with %u entries:\n", routing_table->size);
        for (uint16_t i = 0; i < routing_table->size; i++) {
            sddf_printf("  Route %u: ip=%s subnet=%u interface=%u next_hop=%s\n", i,
                        ipaddr_to_string(routing_table->entries[i].ip, ip_addr_buf0), routing_table->entries[i].subnet,
                        routing_table->entries[i].interface,
                        ipaddr_to_string(routing_table->entries[i].next_hop, ip_addr_buf1));
        }
    }

    fw_queue_init(&webserver, router_config.webserver_rx.queue.vaddr, sizeof(fw_buff_desc_t),
                  router_config.webserver_rx.capacity);

    assert(router_config.packet_queue.vaddr != 0);
    /* Initialise the packet waiting queue from mapped in memory */
    pkt_waiting_init(&pkt_waiting_queue, (void *)router_config.packet_queue.vaddr,
                     router_config.packet_waiting_capacity);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    switch (microkit_msginfo_get_label(msginfo)) {
    case FW_ADD_ROUTE: {
        uint32_t ip = microkit_mr_get(ROUTER_ARG_IP);
        uint8_t subnet = microkit_mr_get(ROUTER_ARG_SUBNET);
        uint32_t next_hop = microkit_mr_get(ROUTER_ARG_NEXT_HOP);
        uint8_t interface = microkit_mr_get(ROUTER_ARG_INTERFACE);

        fw_routing_err_t err = fw_routing_table_add_route(routing_table, interface, ip, subnet, next_hop);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("Router add route. (ip %s, mask %u, next hop %s): %s\n", ipaddr_to_string(ip, ip_addr_buf0),
                        subnet, ipaddr_to_string(next_hop, ip_addr_buf1), fw_routing_err_str[err]);
        }
        microkit_mr_set(ROUTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    case FW_DEL_ROUTE: {
        uint16_t route_id = microkit_mr_get(ROUTER_ARG_ROUTE_ID);
        fw_routing_err_t err = fw_routing_table_remove_route(routing_table, route_id);

        if (FW_DEBUG_OUTPUT) {
            sddf_printf("Router delete route %u: %s\n", route_id, fw_routing_err_str[err]);
        }

        microkit_mr_set(ROUTER_RET_ERR, err);
        return microkit_msginfo_new(0, 1);
    }
    default:
        sddf_printf("ROUTING LOG: unknown request %lu on channel %u\n", microkit_msginfo_get_label(msginfo), ch);
        break;
    }

    return microkit_msginfo_new(0, 0);
}

void notified(microkit_channel ch)
{
    for (uint8_t interface = 0; interface < router_config.num_interfaces; interface++) {
        if (ch == router_config.interfaces[interface].arp_queue.ch) {
            /*
             * This is the channel between the ARP component and the
             * routing component
             */
            process_arp_waiting(interface);
        }
    }

    route();

    for (uint8_t interface = 0; interface < router_config.num_interfaces; interface++) {
        if (notify_arp[interface]) {
            notify_arp[interface] = false;
            microkit_notify(router_config.interfaces[interface].arp_queue.ch);
        }

        if (tx_net[interface]) {
            tx_net[interface] = false;
            microkit_notify(router_config.interfaces[interface].tx_active.ch);
        }

        if (returned[interface]) {
            returned[interface] = false;
            microkit_notify(router_config.interfaces[interface].rx_free.ch);
        }
    }

    if (notify_icmp) {
        notify_icmp = false;
        microkit_notify(router_config.icmp_module.ch);
    }

    if (tx_webserver) {
        tx_webserver = false;
        microkit_notify(router_config.webserver_rx.ch);
    }
}
