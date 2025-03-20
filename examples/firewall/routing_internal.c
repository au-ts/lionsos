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
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/firewall/arp_queue.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/routing.h>
#include <string.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".firewall_router_config"))) firewall_router_config_t router_config;

/* Port that the webserver is on. */
#define WEBSERVER_PROTOCOL 0x06
#define WEBSERVER_PORT 80

serial_queue_handle_t serial_tx_queue_handle;

/* DMA buffer data structures */
firewall_queue_handle_t firewall_filters[FIREWALL_MAX_FILTERS]; /* Filter queues to receive packets */
firewall_queue_handle_t rx_free; /* Queue to return free rx buffers */
firewall_queue_handle_t tx_active; /* Queue to transmit packets out the network */
firewall_queue_handle_t webserver; /* Queue to route to webserver */
uintptr_t data_vaddr; /* Virtual address or rx buffer data region */

/* Arp request/entry data structures */
arp_queue_handle_t *arp_queue; /* This queue holds ARP requests/responses for the arp requester */
arp_table_t arp_table; /* ARP table holding all known ARP entries */
pkts_waiting_t pkt_waiting_queue; /* Queue holding packets awaiting arp responses */

/* Routing data structures */
routing_table_t routing_table; /* Table holding next hop data for subnets */

/* Booleans to keep track of which components need to be notified */
static bool tx_net; /* Packet has been transmitted to the network tx virtualiser */
static bool tx_webserver; /* Packet has been transmitted to the webserver */
static bool returned; /* Buffer has been returned to the rx virtualiser */
static bool notify_arp; /* Arp request has been enqueued */

static void process_arp_waiting(void)
{
    while (!arp_queue_empty_response(arp_queue)) {
        arp_request_t response;
        int err = arp_dequeue_response(arp_queue, &response);
        assert(!err);

        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | Router dequeuing response for ip %u and MAC[0] = %x, MAC[5] = %x\n", router_config.mac_addr[5], response.ip, response.mac_addr[0], response.mac_addr[5]);
        }

        /* Check that we actually have a packet waiting. */
        pkt_waiting_node_t *req_pkt = pkt_waiting_find_node(&pkt_waiting_queue, response.ip);
        if (!req_pkt) {
            continue;
        }

        /* Send or drop all matching ip packets */
        if (response.state == ARP_STATE_UNREACHABLE) {
            /* Invalid response, drop packet associated with the IP address */
            pkt_waiting_node_t *pkt_node = req_pkt;
            for (uint16_t i = 0; i < req_pkt->num_children; i++) {
                err = firewall_enqueue(&rx_free, pkt_node->buffer);
                assert(!err);
                pkt_node = pkts_waiting_next_child(&pkt_waiting_queue, pkt_node);
            }
            /* Free the packet waiting nodes */
            routing_error_t routing_err = pkts_waiting_free_parent(&pkt_waiting_queue, req_pkt);
            assert(routing_err == ROUTING_ERR_OKAY);
        } else {
            /* Substitute the MAC address and send packets out of the NIC */
            pkt_waiting_node_t *pkt_node = req_pkt;
            for (uint16_t i = 0; i < req_pkt->num_children; i++) {
                ipv4_packet_t *tx_pkt = (ipv4_packet_t *)(data_vaddr + pkt_node->buffer.io_or_offset);
                memcpy(tx_pkt->ethdst_addr, response.mac_addr, ETH_HWADDR_LEN);
                memcpy(tx_pkt->ethsrc_addr, router_config.mac_addr, ETH_HWADDR_LEN);
                tx_pkt->check = 0;

                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | Router sending packet for ip %u with buffer number %lu\n", router_config.mac_addr[5], response.ip, req_pkt->buffer.io_or_offset/NET_BUFFER_SIZE);
                }

                err = firewall_enqueue(&tx_active, pkt_node->buffer);
                assert(!err);
                tx_net = true;
                pkt_node = pkts_waiting_next_child(&pkt_waiting_queue, pkt_node);
            }
            /* Free the packet waiting nodes */
            routing_error_t routing_err = pkts_waiting_free_parent(&pkt_waiting_queue, req_pkt);
            assert(routing_err == ROUTING_ERR_OKAY);
        }
    }
}

static void route()
{
    for (int filter = 0; filter < router_config.num_filters; filter++) {
        while (!firewall_queue_empty(&firewall_filters[filter])) {
            firewall_buff_desc_t buffer;
            int err = firewall_dequeue(&firewall_filters[filter], &buffer);
            assert(!err);

            uintptr_t pkt_vaddr = data_vaddr + buffer.io_or_offset;
            ipv4_packet_t *ip_pkt = (ipv4_packet_t *)(pkt_vaddr);

            /* Decrement the TTL field. IF it reaches 0 protocol is that we drop
            * the packet in this router.
            *
            * NOTE: We drop non-IPv4 packets. This case should be handled by the protocol virtualiser.
            */
            if (ip_pkt->ttl > 1 && ip_pkt->type == HTONS(ETH_TYPE_IP)) {
                ip_pkt->ttl -= 1;

                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | Router received packet for ip %u with buffer number %lu\n", router_config.mac_addr[5], ip_pkt->dst_ip, buffer.io_or_offset/NET_BUFFER_SIZE);
                }

                /* Find the next hop address. If we have no route, assume that the device is attached directly */
                routing_entry_t *entry = routing_find_route(&routing_table, ip_pkt->dst_ip);
                
                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | Converted ip %u to next hop ip %u\n", router_config.mac_addr[5], ip_pkt->dst_ip, entry->ip);
                }

                if (entry->out_interface == ROUTING_OUT_INTERNAL) {
                    tcphdr_t *tcp_pkt = (tcphdr_t *)(pkt_vaddr + transport_layer_offset(ip_pkt));

                    /* Webserver only accepts TCP traffic on webserver port */
                    if (ip_pkt->protocol != WEBSERVER_PROTOCOL || tcp_pkt->dst_port == HTONS(WEBSERVER_PORT)) {
                        err = firewall_enqueue(&rx_free, buffer);
                        assert(!err);
                        returned = true;
                        continue;
                    }

                    /* Forward packet to the webserver */
                    err = firewall_enqueue(&webserver, buffer);
                    assert(!err);
                    tx_webserver = true;

                    if (FIREWALL_DEBUG_OUTPUT) {
                        sddf_printf("MAC[5] = %x | Transmitted packet to webserver\n", router_config.mac_addr[5]);
                    }
                } else {
                    arp_entry_t *arp = arp_table_find_entry(&arp_table, entry->ip);
                    if (arp == NULL || arp->state == ARP_STATE_PENDING || arp->state == ARP_STATE_UNREACHABLE) {
                        if (arp->state == ARP_STATE_UNREACHABLE || pkt_waiting_full(&pkt_waiting_queue)) {
                            sddf_dprintf("ROUTING|LOG: Waiting packet queue full or destination unreachable, dropping packet!\n");
                            err = firewall_enqueue(&rx_free, buffer);
                            assert(!err);
                            returned = true;
                        } else {
                            /* In this case, the IP address is not in the ARP Tables.
                            *  We add an entry to the ARP request queue and await a 
                            *  response. If we get a timeout, we will then drop the 
                            *  packets associated with that IP address in the queue.
                            */
                            pkt_waiting_node_t *parent = pkt_waiting_find_node(&pkt_waiting_queue, entry->ip);
                            if (parent) {
                                /* ARP request already enqueued, add node as child. */
                                routing_error_t routing_err = pkt_waiting_push_child(&pkt_waiting_queue, parent, entry->ip, buffer);
                                assert(routing_err == ROUTING_ERR_OKAY);
                            } else if (arp_queue_full_request(arp_queue)) {
                                /* No existing ARP request and queue is full, drop packet. */
                                sddf_dprintf("ROUTING|LOG: ARP request queue full, dropping packet!\n");
                                err = firewall_enqueue(&rx_free, buffer);
                                assert(!err);
                                returned = true;
                            } else {
                                /* Generate ARP request and enqueue packet. */
                                arp_request_t request = {entry->ip, {0}, ARP_STATE_INVALID};
                                err = arp_enqueue_request(arp_queue, request);
                                assert(!err);
                                routing_error_t routing_err = pkt_waiting_push(&pkt_waiting_queue, entry->ip, buffer);
                                assert(routing_err == ROUTING_ERR_OKAY);
                                notify_arp = true;
                            }
                        }
                    } else {
                        /* Match found for MAC address, replace the destination in eth header */
                        memcpy(&ip_pkt->ethdst_addr, &arp->mac_addr, ETH_HWADDR_LEN);
                        memcpy(&ip_pkt->ethsrc_addr, router_config.mac_addr, ETH_HWADDR_LEN);
                        ip_pkt->check = 0;

                        /* Transmit packet out the NIC */
                        if (FIREWALL_DEBUG_OUTPUT) {
                            sddf_printf("MAC[5] = %x | Router sending packet for ip %u mac[5] %x with buffer number %lu\n", router_config.mac_addr[5], entry->ip, ip_pkt->ethdst_addr[5], buffer.io_or_offset/NET_BUFFER_SIZE);
                        }

                        int err = firewall_enqueue(&tx_active, buffer);
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
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
        serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    /* Set up firewall filter queues */
    for (int i = 0; i < router_config.num_filters; i++) {
        firewall_queue_init(&firewall_filters[i], router_config.filters[i].queue.vaddr,
                            router_config.filters[i].capacity);
    }

    /* Set up virt rx firewall queue */
    firewall_queue_init(&rx_free, router_config.rx_free.queue.vaddr,
                        router_config.rx_free.capacity);

    /* Set up virt tx firewall queue */
    firewall_queue_init(&tx_active, router_config.tx_active.queue.vaddr,
        router_config.tx_active.capacity);

    /* Set up router --> webserver queue. */
    firewall_queue_init(&webserver, router_config.rx_active.queue.vaddr,
                        router_config.rx_active.capacity);

    data_vaddr = (uintptr_t)router_config.data.vaddr;

    /* Initialise arp queues */
    arp_queue = (arp_queue_handle_t *) router_config.arp_queue.queue.vaddr;
    arp_handle_init(arp_queue, router_config.arp_queue.capacity);
    arp_table_init(&arp_table, (arp_entry_t *)router_config.arp_cache.vaddr, router_config.arp_cache_capacity);

    /* Initialise routing table */
    routing_entry_t default_entry = {true, ROUTING_OUT_EXTERNAL, 0, 0, 0, 0};
    routing_table_init(&routing_table, default_entry, router_config.webserver.routing_table.vaddr,
        router_config.ip, router_config.webserver.routing_table_capacity);
    
    /* Add an entry for the webserver */
    routing_table_add_rule(&routing_table, ROUTING_OUT_INTERNAL, 0, router_config.ip, 0, router_config.ip);

    /* Initialise the packet waiting queue from mapped in memory */
    pkt_waiting_init(&pkt_waiting_queue, router_config.packet_queue.vaddr, router_config.rx_free.capacity);
}

void notified(microkit_channel ch)
{
    if (ch == router_config.arp_queue.ch) {
        /* This is the channel between the ARP component and the routing component */
        process_arp_waiting();
    } else {
        /* Router has been notified by a filter */
        route();
    }

    if (notify_arp) {
        notify_arp = false;
        microkit_notify(router_config.arp_queue.ch);
    }

    if (tx_webserver) {
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
