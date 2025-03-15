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
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <config.h>
#include <routing.h>
#include <firewall_arp.h>
#include <hashmap.h>
#include <linkedlist.h>
#include <protocols.h>
#include <string.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".firewall_router_config"))) firewall_router_config_t router_config;

serial_queue_handle_t serial_tx_queue_handle;

typedef struct state {
    firewall_queue_handle_t rx_free;
    firewall_queue_handle_t tx_active;
    firewall_queue_handle_t firewall_filters[LIONSOS_FIREWALL_MAX_FILTERS];
    uintptr_t data_vaddr;
} state_t;

state_t state;

/* This queue holds ARP requests/responses for the arp requester */
arp_queue_handle_t *arp_queue;

/* Queue holding packets awaiting arp responses */
struct ll_info pkt_waiting_queue;

/* ARP table holding all known ARP entries */
hashtable_t *arp_table;
routing_entry_t routing_table[NUM_ROUTES] = {{0}};

/* Booleans to keep track of which components need to be notified */
static bool transmitted;
static bool returned;
static bool notify_arp;

/* Return node with maching ip, or null. */
static void *ll_node_find(struct ll_info *info, uint32_t ip)
{
    struct llnode_ptrs *curr = LLNODE_PTRS_CAST(info->head);
    while (curr != NULL) {
        llnode_pkt_waiting_t *curr_node = (llnode_pkt_waiting_t *) curr;
        if (curr_node->ip == ip) {
            return (void *) curr;
        }
        curr = LLNODE_PTRS_CAST(curr->next);
    }

    return NULL;
}

/* Check if there is a packet with this IP address already waiting on an ARP reply. */
static bool check_waiting(struct ll_info *info, uint32_t ip)
{
    struct llnode_ptrs *curr = LLNODE_PTRS_CAST(info->head);
    while (curr != NULL) {
        llnode_pkt_waiting_t *curr_node = (llnode_pkt_waiting_t *) curr;
        if (curr_node->ip == ip) {
            return true;
        }
        curr = LLNODE_PTRS_CAST(curr->next);
    }

    return false;
}

static uint32_t find_route(uint32_t ip)
{
    /* TODO: extend this function to match with longest subnet mask
    or least hops on tie */
    for (int i = 0; i < NUM_ROUTES; i++) {
        /* Check this entry is valid */
        if (routing_table[i].subnet_mask) {
            if ((ip & routing_table[i].subnet_mask) == (routing_table[i].network_id & routing_table[i].subnet_mask)) {
                return routing_table[i].next_hop;
            }
        }
    }

    return ip;
}

static void process_arp_waiting(void)
{
    while (!arp_queue_empty_response(arp_queue)) {
        arp_request_t response;
        int err = arp_dequeue_response(arp_queue, &response);
        assert(!err);

        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | Router dequeuing response for ip %u and MAC[0] = %x, MAC[5] = %x\n", router_config.mac_addr[5], response.ip_addr, response.mac_addr[0], response.mac_addr[5]);
        }

        /* Check that we actually have a packet waiting. */
        llnode_pkt_waiting_t *waiting_packet = (llnode_pkt_waiting_t *) ll_node_find(&pkt_waiting_queue, response.ip_addr);
        if (!waiting_packet) {
            continue;
        }

        /* TODO: search for other matching packets and send them */
        if (!response.valid && waiting_packet->valid) {
            /* Invalid response, drop packet associated with the IP address */
            waiting_packet->buffer.len = 0;
            err = firewall_enqueue(&state.rx_free, waiting_packet->buffer);
            assert(!err);
        } else {
            /* Substitute the MAC address and send the packet out of the NIC */
            struct ipv4_packet *pkt = (struct ipv4_packet *)(state.data_vaddr + waiting_packet->buffer.io_or_offset);
            memcpy(pkt->ethdst_addr, response.mac_addr, ETH_HWADDR_LEN);
            memcpy(pkt->ethsrc_addr, router_config.mac_addr, ETH_HWADDR_LEN);
            pkt->check = 0;

            if (FIREWALL_DEBUG_OUTPUT) {
                sddf_printf("MAC[5] = %x | Router sending packet for ip %u with buffer number %lu\n", router_config.mac_addr[5], response.ip_addr, waiting_packet->buffer.io_or_offset/NET_BUFFER_SIZE);
            }

            err = firewall_enqueue(&state.tx_active, waiting_packet->buffer);
            assert(!err);
            transmitted = true;
        }

        llfree(&pkt_waiting_queue, (void *)waiting_packet);
    }

}

static void route()
{
    for (int filter = 0; filter < router_config.num_filters; filter++) {
        while (!firewall_queue_empty(&state.firewall_filters[filter])) {
            firewall_buff_desc_t buffer;
            int err = firewall_dequeue(&state.firewall_filters[filter], &buffer);
            assert(!err);

            struct ipv4_packet *pkt = (struct ipv4_packet *)(state.data_vaddr + buffer.io_or_offset);

            /* Decrement the TTL field. IF it reaches 0 protocol is that we drop
            * the packet in this router.
            *
            * NOTE: We drop non-IPv4 packets. This case should be handled by the protocol virtualiser.
            */
            if (pkt->ttl > 1 && pkt->type == HTONS(ETH_TYPE_IP)) {
                pkt->ttl -= 1;

                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | Router received packet for ip %u with buffer number %lu\n", router_config.mac_addr[5], pkt->dst_ip, buffer.io_or_offset/NET_BUFFER_SIZE);
                }

                /* Find the next hop address. If we have no route, assume that the device is attached directly */
                uint32_t nextIP = find_route(pkt->dst_ip);

                if (FIREWALL_DEBUG_OUTPUT) {
                    sddf_printf("MAC[5] = %x | Converted ip %u to next hop ip %u\n", router_config.mac_addr[5], pkt->dst_ip, nextIP);
                }

                arp_entry_t hash_entry;
                int ret = hashtable_search(arp_table, (uint32_t) nextIP, &hash_entry);
                if (ret == -1) {
                    if (llfull(&pkt_waiting_queue)) {
                        sddf_dprintf("ROUTING|LOG: Waiting packet queue full, dropping packet!\n");
                        buffer.len = 0;
                        err = firewall_enqueue(&state.rx_free, buffer);
                        assert(!err);
                        returned = true;
                    } else {
                        /* In this case, the IP address is not in the ARP Tables.
                        *  We add an entry to the ARP request queue and await a 
                        *  response. If we get a timeout, we will then drop the 
                        *  packets associated with that IP address in the queue.
                        */
                        if (!check_waiting(&pkt_waiting_queue, nextIP)) {
                            if (arp_queue_full_request(arp_queue)) {
                                sddf_dprintf("ROUTING|LOG: ARP request queue full, dropping packet!\n");
                                buffer.len = 0;
                                err = firewall_enqueue(&state.rx_free, buffer);
                                assert(!err);
                                returned = true;
                                continue;
                            } else {
                                int err = arp_enqueue_request(arp_queue, nextIP);
                                assert(!err);
                            }
                        }

                        /* Add packet to the ARP waiting queue */
                        llnode_pkt_waiting_t *waiting_packet = (llnode_pkt_waiting_t *) llalloc(&pkt_waiting_queue);
                        waiting_packet->ip = nextIP;
                        waiting_packet->buffer = buffer;
                        waiting_packet->valid = true;
                        llpush(&pkt_waiting_queue, (void *)waiting_packet);
                        notify_arp = true;
                    }
                } else {
                    /* Match found for MAC address, replace the destination in eth header */
                    memcpy(&pkt->ethdst_addr, &hash_entry.mac_addr, ETH_HWADDR_LEN);
                    memcpy(&pkt->ethsrc_addr, router_config.mac_addr, ETH_HWADDR_LEN);
                    pkt->check = 0;

                    /* Transmit packet our the NIC */
                    if (FIREWALL_DEBUG_OUTPUT) {
                        sddf_printf("MAC[5] = %x | Router sending packet for ip %u mac[5] %u with buffer number %lu\n", router_config.mac_addr[5], nextIP, pkt->ethdst_addr[5], buffer.io_or_offset/NET_BUFFER_SIZE);
                    }

                    int err = firewall_enqueue(&state.tx_active, buffer);
                    assert(!err);
                    transmitted = true;
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
        firewall_queue_init(&state.firewall_filters[i], router_config.filters[i].queue.vaddr,
                            router_config.filters[i].capacity);
    }

    /* Set up virt tx firewall queue */
    firewall_queue_init(&state.tx_active, router_config.tx_active.queue.vaddr,
                        router_config.tx_active.capacity);

    /* Set up virt rx firewall queue */
    firewall_queue_init(&state.rx_free, router_config.rx_free.conn.queue.vaddr,
                        router_config.rx_free.conn.capacity);

    state.data_vaddr = (uintptr_t)router_config.rx_free.data.region.vaddr;

    /* Initialise arp queues */
    arp_queue = (arp_queue_handle_t *) router_config.arp.arp_queue.queue.vaddr;
    arp_handle_init(arp_queue, arp_queue->capacity);

    arp_table = (hashtable_t*) router_config.arp.arp_cache.vaddr;

    /* Initialise the packet waiting queue from mapped in memory */
    pkt_waiting_queue.llnode_pool = (uint8_t *) router_config.packet_queue.vaddr;
    pkt_waiting_queue.pool_size = 10;
    pkt_waiting_queue.node_size = sizeof(llnode_pkt_waiting_t);

    llinit(&pkt_waiting_queue);
}

void notified(microkit_channel ch)
{
    if (ch == router_config.arp.arp_queue.ch) {
        /* This is the channel between the ARP component and the routing component */
        process_arp_waiting();
    } else {
        /* Router has been notified by a filter */
        route();
    }

    if (notify_arp) {
        notify_arp = false;
        microkit_notify(router_config.arp.arp_queue.ch);
    }

    if (returned) {
        returned = false;
        microkit_deferred_notify(router_config.rx_free.conn.ch);
    }

    if (transmitted) {
        transmitted = false;
        microkit_notify(router_config.tx_active.ch);
    }
}
