/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

// @kwinter: This is for a basic inbound packet router.

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
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <string.h>

#include "routing.h"
#include "firewall_arp.h"
#include "hashmap.h"
#include "firewall_config.h"
#include "linkedlist.h"
#include "protocols.h"

#define WEB_GUI_ID 100
__attribute__((__section__(".router_config"))) router_config_external_t router_config;

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

hashtable_t *arp_table;
routing_entry_t routing_table[NUM_ROUTES] = {{0}};

/* Booleans to indicate whether packets have been enqueued during notification handling */
static bool notify_tx;
static bool notify_rx;

net_queue_handle_t virt_tx_queue;
net_queue_handle_t webserver_tx_queue;

typedef struct state {
    net_queue_handle_t filter_queue[61];
} state_t;

state_t state;

struct ll_info pkt_waiting_queue;

/* This queue will hold packets that we need to generate an ARP request for. */
net_queue_handle_t arp_waiting;
/* This queue will hold all the ARP requests/responses that are needed by the
packets in the arp_waiting queue. */
arp_queue_handle_t *arp_queries;

dev_info_t *device_info;

void *ll_node_find(struct ll_info *info, uint32_t ip)
{
    // Loop through the waiting list
    struct llnode_ptrs *curr = LLNODE_PTRS_CAST(info->head);
    while (curr != NULL) {
        // Cast to llnode_pkt_waiting
        struct llnode_pkt_waiting *curr_node = (struct llnode_pkt_waiting *) curr;
        if (curr_node->ip == ip) {
            return (void *) curr;
        }
        curr = LLNODE_PTRS_CAST(curr->next);
    }

    return NULL;
}

/* Check if there is a packet with this IP address already waiting on an ARP reply. */
bool check_waiting(struct ll_info *info, uint32_t ip)
{
    if (ll_node_find(info, ip) == NULL) {
        return false;
    } else {
        return true;
    }
}


void process_arp_waiting()
{
    /* Loop through all of the ARP responses. If there are any invalid
    responses we will drop the packets associated with the IP address. Otherwise
    we will substitute the MAC address in, and then send the packet out of the NIC. */
    while (!arp_queue_empty_response(arp_queries)) {
        arp_request_t response;
        int err = arp_dequeue_response(arp_queries, &response);
        /* Check that we actually have a packet waiting. */
        struct llnode_pkt_waiting *waiting_packet = (struct llnode_pkt_waiting *) ll_node_find(&pkt_waiting_queue, response.ip_addr);
        if (waiting_packet == NULL) {
            sddf_dprintf("There where no packets waiting.")
        }
        sddf_dprintf("ROUTING_EXTERNAL|Processing arp waiting queue\n");
        if (waiting_packet == NULL) {
            sddf_dprintf("ROUTING_INTERNAL|We received and arp response for a packet not waiting\n");
            continue;
        } else if ((!response.valid && waiting_packet->valid)) {
            // Find all packets with this IP address and drop them.
            waiting_packet->buffer.len = 0;
            err = net_enqueue_free(&state.filter_queue[waiting_packet->filter], waiting_packet->buffer);
            assert(!err);
        } else if (waiting_packet->valid && waiting_packet->filter == WEB_GUI_ID) {
            if (response.ip_addr == waiting_packet->ip) {
                struct ipv4_packet *pkt = (struct ipv4_packet *)(router_config.webserver_conn.data.vaddr + waiting_packet->buffer.io_or_offset);
                sddf_memcpy(pkt->ethdst_addr, response.mac_addr, ETH_HWADDR_LEN);
                sddf_memcpy(pkt->ethsrc_addr, device_info->mac, ETH_HWADDR_LEN);
                net_buff_desc_t buffer_tx;
                int err = net_dequeue_free(&virt_tx_queue, &buffer_tx);
                assert(!err);
                pkt->check = 0;

                // @kwinter: For now we are memcpy'ing the packet from our receive buffer
                // to the transmit buffer.
                sddf_memcpy((net_config.tx_data.vaddr + buffer_tx.io_or_offset), (router_config.webserver_conn.data.vaddr + waiting_packet->buffer.io_or_offset), waiting_packet->buffer.len);
                buffer_tx.len = waiting_packet->buffer.len;
                err = net_enqueue_active(&virt_tx_queue, buffer_tx);
                assert(!err);
                waiting_packet->buffer.len = 0;
                err = net_enqueue_free(&webserver_tx_queue, waiting_packet->buffer);
                assert(!err);
                microkit_deferred_notify(net_config.tx.id);
            }
        } else if (waiting_packet->valid) {
            if (response.ip_addr == waiting_packet->ip) {
                struct ipv4_packet *pkt = (struct ipv4_packet *)(router_config.filters[waiting_packet->filter].data.vaddr + waiting_packet->buffer.io_or_offset);
                sddf_memcpy(pkt->ethdst_addr, response.mac_addr, ETH_HWADDR_LEN);
                sddf_memcpy(pkt->ethsrc_addr, device_info->mac, ETH_HWADDR_LEN);
                net_buff_desc_t buffer_tx;
                int err = net_dequeue_free(&virt_tx_queue, &buffer_tx);
                assert(!err);
                pkt->check = 0;

                // @kwinter: For now we are memcpy'ing the packet from our receive buffer
                // to the transmit buffer.
                sddf_memcpy((net_config.tx_data.vaddr + buffer_tx.io_or_offset), (router_config.filters[waiting_packet->filter].data.vaddr + waiting_packet->buffer.io_or_offset), waiting_packet->buffer.len);
                buffer_tx.len = waiting_packet->buffer.len;
                err = net_enqueue_active(&virt_tx_queue, buffer_tx);
                assert(!err);
                waiting_packet->buffer.len = 0;
                err = net_enqueue_free(&state.filter_queue[waiting_packet->filter], waiting_packet->buffer);
                assert(!err);
                microkit_deferred_notify(net_config.tx.id);
            }
        } else {
            sddf_dprintf("ROUTING_EXTERNAL|Unable to find packet waiting on ip address from ARP response.\n");
        }

        llfree(&pkt_waiting_queue, (void *)waiting_packet);
    }

}

uint32_t find_route(uint32_t ip)
{
    // TODO: extend this function to match with the longest subnet mask,
    // and if tied in this step, find the route with the least hops.
    for (int i = 0; i < NUM_ROUTES; i++) {
        if ((ip & routing_table[i].subnet_mask) == (routing_table[i].network_id & routing_table[i].subnet_mask)) {
            // We have a match, return the next hop IP address.
            return routing_table[i].next_hop;
        }
    }

    // If we have gotten here, assume on the default gateway.
    return 0;
}

void route_webserver()
{
    bool transmitted = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&webserver_tx_queue) && !net_queue_empty_free(&virt_tx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&webserver_tx_queue, &buffer);
            assert(!err);

            struct ipv4_packet *pkt = (struct ipv4_packet *)(router_config.webserver_conn.data.vaddr + buffer.io_or_offset);
            if (pkt == NULL) {
                sddf_dprintf("ROUTING_EXTERNAL| We got a null packet from Micropython!\n");
                buffer.len = 0;
                err = net_enqueue_free(&webserver_tx_queue, buffer);
                assert(!err);
                continue;
            }
            /* Decrement the TTL field. IF it reaches 0 protocol is that we drop
            * the packet in this router.
            *
            * NOTE: We assume that if we get a packet other than an IPv4 packet, we drop.buffer
            * This edge case should be handled by a new protocol virtualiser.
            */
            if (pkt->ttl > 1 && pkt->type == HTONS(ETH_TYPE_IP)) {
                pkt->ttl -= 1;
                // This is where we will swap out the MAC address with the appropriate address
                uint32_t destIP = pkt->dst_ip;

                // From the destination IP, consult the routing tables to find the next hop address.
                uint32_t nextIP = find_route(destIP);
                if (nextIP == 0) {
                    // If we have no route, assume that the device is attached directly.
                    nextIP = destIP;
                }
                uint8_t mac[ETH_HWADDR_LEN];
                arp_entry_t hash_entry;
                int ret = hashtable_search(arp_table, (uint32_t) nextIP, &hash_entry);
                if (ret == -1 && !llfull(&pkt_waiting_queue)) {
                    /* In this case, the IP address is not in the ARP Tables.
                    *  We add an entry to the ARP request queue. This is where we
                    *  place the responses to the ARP requests, and if we get a
                    *  timeout, we will then drop the packets associated with that IP address
                    *  that are waiting in the queue.
                    */
                    if (!arp_queue_full_request(arp_queries) && !check_waiting(&pkt_waiting_queue, destIP)) {
                        arp_request_t request = {0};
                        request.ip_addr = nextIP;
                        request.valid = true;
                        char buf[16];
                        int ret = arp_enqueue_request(arp_queries, request);
                        if (ret != 0) {
                            sddf_dprintf("ROUTING_EXTERNAL| Unable to enqueue into ARP request queue!\n");
                        }
                    } else {
                        buffer.len = 0;
                        err = net_enqueue_free(&webserver_tx_queue, buffer);
                        assert(!err);
                        continue;
                    }

                    // Add this packet to the ARP waiting queue
                    struct llnode_pkt_waiting *waiting_packet = (struct llnode_pkt_waiting *) llalloc(&pkt_waiting_queue);
                    waiting_packet->ip = nextIP;
                    sddf_memcpy(&waiting_packet->buffer, &buffer, sizeof(net_buff_desc_t));
                    waiting_packet->buffer = buffer;
                    waiting_packet->valid = true;
                    waiting_packet->filter = 100;
                    llpush(&pkt_waiting_queue, (void *)waiting_packet);
                    microkit_deferred_notify(router_config.router.id);
                    continue;
                } else {
                    // We should have the mac address. Replace the dest in the ethernet header.
                    sddf_memcpy(&pkt->ethdst_addr, &hash_entry.mac_addr, ETH_HWADDR_LEN);
                    sddf_memcpy(&pkt->ethsrc_addr, device_info->mac, ETH_HWADDR_LEN);
                    pkt->check = 0;
                    // Send the packet out to the network.
                    net_buff_desc_t buffer_tx;
                    int err = net_dequeue_free(&virt_tx_queue, &buffer_tx);
                    assert(!err);

                    // @kwinter: For now we are memcpy'ing the packet from our receive buffer
                    // to the transmit buffer.
                    sddf_memcpy((net_config.tx_data.vaddr + buffer_tx.io_or_offset), (router_config.webserver_conn.data.vaddr + buffer.io_or_offset), buffer.len + (sizeof(struct ipv4_packet)));
                    struct ipv4_packet *test = (struct ipv4_packet *)(net_config.tx_data.vaddr + buffer_tx.io_or_offset);
                    buffer_tx.len = buffer.len;
                    err = net_enqueue_active(&virt_tx_queue, buffer_tx);
                    transmitted = true;

                    assert(!err);
                }
                buffer.len = 0;
                err = net_enqueue_free(&webserver_tx_queue, buffer);
                assert(!err);

            }
        }

        net_request_signal_active(&webserver_tx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&webserver_tx_queue)) {
            net_cancel_signal_active(&webserver_tx_queue);
            reprocess = true;
        }
    }

}

void route()
{
    // Check the IP address of the packet.
    bool transmitted = false;
    for (int filter = 0; filter < router_config.num_filters; filter++) {
        bool reprocess = true;
        while (reprocess) {
            while (!net_queue_empty_active(&state.filter_queue[filter]) && !net_queue_empty_free(&virt_tx_queue)) {
                net_buff_desc_t buffer;
                int err = net_dequeue_active(&state.filter_queue[filter], &buffer);
                assert(!err);

                struct ipv4_packet *pkt = (struct ipv4_packet *)(router_config.filters[filter].data.vaddr + buffer.io_or_offset);

                /* Decrement the TTL field. IF it reaches 0 protocol is that we drop
                * the packet in this router.
                *
                * NOTE: We assume that if we get a packet other than an IPv4 packet, we drop.buffer
                * This edge case should be handled by a new protocol virtualiser.
                */
                if (pkt->ttl > 1 && pkt->type == HTONS(ETH_TYPE_IP)) {
                    pkt->ttl -= 1;
                    // This is where we will swap out the MAC address with the appropriate address
                    uint32_t destIP = pkt->dst_ip;

                    // From the destination IP, consult the routing tables to find the next hop address.
                    uint32_t nextIP = find_route(destIP);
                    if (nextIP == 0) {
                        // If we have no route, assume that the device is attached directly.
                        nextIP = destIP;
                    }
                    uint8_t mac[ETH_HWADDR_LEN];
                    arp_entry_t hash_entry;
                    int ret = hashtable_search(arp_table, (uint32_t) nextIP, &hash_entry);
                    if (ret == -1 && !llfull(&pkt_waiting_queue)) {
                        /* In this case, the IP address is not in the ARP Tables.
                        *  We add an entry to the ARP request queue. This is where we
                        *  place the responses to the ARP requests, and if we get a
                        *  timeout, we will then drop the packets associated with that IP address
                        *  that are waiting in the queue.
                        */
                        if (!arp_queue_full_request(arp_queries) && !check_waiting(&pkt_waiting_queue, destIP)) {
                            arp_request_t request = {0};
                            request.ip_addr = nextIP;
                            request.valid = true;
                            char buf[16];
                            int ret = arp_enqueue_request(arp_queries, request);
                            if (ret != 0) {
                                sddf_dprintf("ROUTING_EXTERNAL| Unable to enqueue into ARP request queue!\n");
                            }
                        } else {
                            buffer.len = 0;
                            err = net_enqueue_free(&state.filter_queue[filter], buffer);
                            assert(!err);
                            continue;
                        }

                        // Add this packet to the ARP waiting queue
                        struct llnode_pkt_waiting *waiting_packet = (struct llnode_pkt_waiting *) llalloc(&pkt_waiting_queue);
                        waiting_packet->ip = nextIP;
                        sddf_memcpy(&waiting_packet->buffer, &buffer, sizeof(net_buff_desc_t));
                        waiting_packet->buffer = buffer;
                        waiting_packet->valid = true;
                        waiting_packet->filter = filter;
                        llpush(&pkt_waiting_queue, (void *)waiting_packet);
                        microkit_deferred_notify(router_config.router.id);
                        continue;
                    } else {
                        // We should have the mac address. Replace the dest in the ethernet header.
                        sddf_memcpy(&pkt->ethdst_addr, &hash_entry.mac_addr, ETH_HWADDR_LEN);
                        sddf_memcpy(&pkt->ethsrc_addr, device_info->mac, ETH_HWADDR_LEN);
                        pkt->check = 0;
                        // Send the packet out to the network.
                        net_buff_desc_t buffer_tx;
                        int err = net_dequeue_free(&virt_tx_queue, &buffer_tx);
                        assert(!err);

                        // @kwinter: For now we are memcpy'ing the packet from our receive buffer
                        // to the transmit buffer.
                        sddf_memcpy((net_config.tx_data.vaddr + buffer_tx.io_or_offset), (router_config.filters[filter].data.vaddr + buffer.io_or_offset), buffer.len + (sizeof(struct ipv4_packet)));
                        struct ipv4_packet *test = (struct ipv4_packet *)(net_config.tx_data.vaddr + buffer_tx.io_or_offset);
                        buffer_tx.len = buffer.len;
                        err = net_enqueue_active(&virt_tx_queue, buffer_tx);
                        transmitted = true;

                        assert(!err);
                    }
                    buffer.len = 0;
                    err = net_enqueue_free(&state.filter_queue[filter], buffer);
                    assert(!err);

                }
            }

            net_request_signal_active(&state.filter_queue[filter]);
            reprocess = false;

            if (!net_queue_empty_active(&state.filter_queue[filter])) {
                net_cancel_signal_active(&state.filter_queue[filter]);
                reprocess = true;
            }
        }
    }

    if (transmitted && net_require_signal_active(&virt_tx_queue)) {
        net_cancel_signal_active(&virt_tx_queue);
        microkit_deferred_notify(net_config.tx.id);
    }

}

void init(void)
{
    // Init the hashtable here, as we are the first component that will
    // ever access it.
    assert(net_config_check_magic((void *)&net_config));
    assert(firewall_config_check_magic((void*) &router_config));
    arp_table = (hashtable_t*) router_config.router.arp_cache.vaddr;
    hashtable_init(arp_table);

    // Setup all the queues for the filters
    for (int i = 0; i < router_config.num_filters; i++) {
        net_queue_init(&state.filter_queue[i], router_config.filters[i].conn.free_queue.vaddr,
            router_config.filters[i].conn.active_queue.vaddr, router_config.filters[i].conn.num_buffers);
    }
    net_queue_init(&virt_tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
        net_config.tx.num_buffers);
    net_buffers_init(&virt_tx_queue, 0);

    net_queue_init(&webserver_tx_queue, router_config.webserver_conn.conn.free_queue.vaddr,
        router_config.webserver_conn.conn.active_queue.vaddr, router_config.webserver_conn.conn.num_buffers);

    arp_queries = (arp_queue_handle_t *) router_config.router.arp_queue.vaddr;
    arp_handle_init(arp_queries, 256);

    device_info = (dev_info_t *) net_config.dev_info.vaddr;

    // Init the waiting queue from a pool of memory
    pkt_waiting_queue.llnode_pool = (uint8_t *) router_config.packet_queue.vaddr;
    pkt_waiting_queue.pool_size = 10;
    pkt_waiting_queue.node_size = sizeof(struct llnode_pkt_waiting);
    pkt_waiting_queue.empty_head = NULL;
    pkt_waiting_queue.head = NULL;
    pkt_waiting_queue.tail = NULL;

    llinit(&pkt_waiting_queue);
    // routing_table[0].network_id = 0;
    // routing_table[0].subnet_mask = 0xFFFFFF00;
    // routing_table[0].next_hop = 0;
}

void notified(microkit_channel ch)
{
    // Popualate with the rx ch number
    if (ch == router_config.router.id) {
        /* This is the channel between the ARP component and the routing component. */
        process_arp_waiting();
    } else if (ch == router_config.webserver_conn.conn.id) {
        sddf_dprintf("ROUTING_EXTERNAL|Sending some stuff for micropython!\n");
        route_webserver();
    } else {
        route();
    }
}
