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
#include <lions/firewall/config.h>
#include <lions/firewall/hashmap.h>
#include <lions/firewall/protocols.h>
#include <string.h>

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".firewall_arp_requester_config"))) firewall_arp_requester_config_t arp_config;

uint8_t empty_mac[ETH_HWADDR_LEN] = {0,0,0,0,0,0};

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

/* Queues hold ARP requests/responses for router and webserver */
arp_queue_handle_t *arp_queues[FIREWALL_NUM_ARP_REQUESTER_CLIENTS];

/* ARP table caches ARP request responses */
hashtable_t *arp_table;

/* Time that we will flush the arp queue (to the closest arp retry timer tick). */
uint64_t time_to_flush;

void generate_arp(arp_packet_t *pkt, uint32_t ip_addr)
{
        /* Set the destination MAC address as the broadcast MAC address */
        memset(&pkt->ethdst_addr, 0xFF, ETH_HWADDR_LEN);
        memcpy(&pkt->ethsrc_addr, arp_config.mac_addr, ETH_HWADDR_LEN);
        memcpy(&pkt->hwsrc_addr, arp_config.mac_addr, ETH_HWADDR_LEN);

        pkt->type = HTONS(ETH_TYPE_ARP);
        pkt->hwtype = HTONS(ETH_HWTYPE);
        pkt->proto = HTONS(ETH_TYPE_IP);
        pkt->hwlen = ETH_HWADDR_LEN;
        pkt->protolen = IPV4_PROTO_LEN;
        pkt->opcode = HTONS(ETHARP_OPCODE_REQUEST);

        /* Memset the hardware src addr to 0 for ARP requests */
        memset(&pkt->hwdst_addr, 0, ETH_HWADDR_LEN);
        pkt->ipdst_addr = ip_addr;
        pkt->ipsrc_addr = arp_config.ip;
        memset(&pkt->padding, 0, 10);

}

void process_requests()
{
    bool transmitted = false;
    for (uint8_t client = 0; client < FIREWALL_NUM_ARP_REQUESTER_CLIENTS; client++) {
        while (!arp_queue_empty_request(arp_queues[client]) && !net_queue_empty_free(&tx_queue)) {
            arp_request_t request;
            int err = arp_dequeue_request(arp_queues[client], &request);
            assert(!err && request.valid);

            /* Generate ARP request */
            net_buff_desc_t buffer = {};
            err = net_dequeue_free(&tx_queue, &buffer);
            assert(!err);

            arp_packet_t *pkt = (arp_packet_t *)(net_config.tx_data.vaddr + buffer.io_or_offset);

            generate_arp(pkt, request.ip_addr);

            if (FIREWALL_DEBUG_OUTPUT) {
                sddf_printf("MAC[5] = %x | ARP requester processing client %u request for ip %u\n", arp_config.mac_addr[5], client, request.ip_addr);
            }

            buffer.len = 56;
            err = net_enqueue_active(&tx_queue, buffer);
            assert(!err);

            /* Create arp entry for request */
            arp_entry_t entry = {0};
            entry.state = pending;
            entry.time = sddf_timer_time_now(timer_config.driver_id);
            entry.client = client;
            memcpy(entry.mac_addr, empty_mac, ETH_HWADDR_LEN);
            err = hashtable_insert(arp_table, request.ip_addr, &entry);

            transmitted = true;
            request.valid = false;
        }
    }

    if (transmitted && net_require_signal_active(&tx_queue)) {
        net_cancel_signal_active(&tx_queue);
        microkit_deferred_notify(net_config.tx.id);
    }
}

void process_responses()
{
    bool notify_client[FIREWALL_NUM_ARP_REQUESTER_CLIENTS] = {false};
    bool returned = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            arp_packet_t *pkt = (arp_packet_t *)(net_config.rx_data.vaddr + buffer.io_or_offset);
            /* Check if packet is an ARP request */
            if (pkt->type == HTONS(ETH_TYPE_ARP)) {
                /* Check if it's a probe, ignore announcements */
                if (pkt->opcode == HTONS(ETHARP_OPCODE_REPLY)) {
                    /* Find the arp entry */
                    arp_entry_t entry = {0};
                    int found = hashtable_search(arp_table, pkt->ipsrc_addr, &entry);
                    if (found && entry.state == pending) {
                        /* This was a response to a request we sent, update entry */
                        memcpy(entry.mac_addr, pkt->hwsrc_addr, ETH_HWADDR_LEN);
                        entry.state = positive;
                        err = hashtable_update(arp_table, pkt->ipsrc_addr, &entry);
                        if (err == -1) {
                            sddf_dprintf("MAC[5] = %x | ARP requester error updating hashtable!\n", arp_config.mac_addr[5]);
                        }

                        if (FIREWALL_DEBUG_OUTPUT) {
                            sddf_printf("MAC[5] = %x | ARP requester received response for ip %u. MAC[0] = %x, MAC[5] = %x\n", arp_config.mac_addr[5], pkt->ipsrc_addr, pkt->hwsrc_addr[0], pkt->hwsrc_addr[5]);
                        }

                        /* Send to router */
                        arp_enqueue_response(arp_queues[entry.client], pkt->ipsrc_addr, entry.mac_addr, true);

                        notify_client[entry.client] = true;
                    } else {
                        arp_entry_t new_entry = {0};
                        memcpy(&new_entry.mac_addr, &pkt->hwsrc_addr, ETH_HWADDR_LEN);
                        new_entry.state = positive;
                        // This new entry does not have an associtated client.
                        err = hashtable_insert(arp_table, pkt->ipsrc_addr, &new_entry);
                        if (err) {
                            sddf_dprintf("ARP_REQUESTER|LOG: Hash table full, failed to insert!\n");
                        }
                    }
                }
            }

            buffer.len = 0;
            err = net_enqueue_free(&rx_queue, buffer);
            assert(!err);
            returned = true;
        }

        net_request_signal_active(&rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue)) {
            net_cancel_signal_active(&rx_queue);
            reprocess = true;
        }
    }

    if (returned && net_require_signal_free(&rx_queue)) {
        net_cancel_signal_free(&rx_queue);
        microkit_deferred_notify(net_config.rx.id);
    }

    for (uint8_t client = 0; client < FIREWALL_NUM_ARP_REQUESTER_CLIENTS; client++) {
        if (notify_client[client]) {
            microkit_notify(arp_config.clients[client].ch);
        }
    }
}

/* Returns the number of ARP's still requiring a retry. */
int process_retries(uint64_t time)
{
    int pending_pkts = 0;
    // Loop over hashmap, and check if we have any pending retries.
    for (int i = 0; i < 100; i++) {
        if (arp_table->used[i] == 1 ) {
            if (arp_table->entries[i].value.state == pending) {
                if (arp_table->entries[i].value.num_retries >= ARP_MAX_RETRIES) {
                    // ARP table has been expired
                    arp_table->entries[i].value.state = negative;
                    // Generate an ARP response packet for the router.
                    arp_enqueue_response(arp_queues[arp_table->entries[i].value.client], arp_table->entries[i].key, empty_mac, false);
                } else {
                    // Resend the ARP request out to the network
                    /* Generate ARP request */
                    pending_pkts++;

                    net_buff_desc_t buffer = {};
                    int err = net_dequeue_free(&tx_queue, &buffer);
                    if (err) {
                        sddf_dprintf("ARP_REQUESTER| Unable to requeue ARP request as tx queue is full!\n");
                        return pending_pkts;
                    }

                    arp_packet_t *pkt = (arp_packet_t *)(net_config.tx_data.vaddr + buffer.io_or_offset);

                    generate_arp(pkt, arp_table->entries[i].key);

                    if (FIREWALL_DEBUG_OUTPUT) {
                        sddf_printf("MAC[5] = %x | ARP requester processing request for ip %u\n", arp_config.mac_addr[5], arp_table->entries[i].key);
                    }

                    buffer.len = 56;
                    err = net_enqueue_active(&tx_queue, buffer);
                    assert(!err);

                    // Increment the number of retries
                    arp_table->entries[i].value.num_retries++;

                    microkit_deferred_notify(net_config.tx.id);
                }
            }
        }
    }
    return pending_pkts;
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
        serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
        net_config.rx.num_buffers);
    net_queue_init(&tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
        net_config.tx.num_buffers);
    net_buffers_init(&tx_queue, 0);

    for (uint8_t client = 0; client < FIREWALL_NUM_ARP_REQUESTER_CLIENTS; client++) {
        arp_queues[client] = (arp_queue_handle_t *) arp_config.clients[client].queue.vaddr;
        arp_handle_init(arp_queues[client], arp_config.clients[client].capacity);
    }

    /* Set the period for the arp cache flush. */
    time_to_flush = sddf_timer_time_now(timer_config.driver_id) + ARP_CACHE_LIFE_US;

    // @kwinter: Not sure if this is the best place to start the tick.
    sddf_timer_set_timeout(timer_config.driver_id, ARP_RETRY_TIMER_S * NS_IN_S);
}

void notified(microkit_channel ch)
{
    if (ch == arp_config.clients[0].ch || ch == arp_config.clients[1].ch) {
        process_requests();
    } if (ch == net_config.rx.id) {
        process_responses();
    } else if (ch == timer_config.driver_id) {
        uint64_t time = sddf_timer_time_now(timer_config.driver_id);
        process_retries(time);
        sddf_timer_set_timeout(timer_config.driver_id, ARP_RETRY_TIMER_S * NS_IN_S);

        if (time >= time_to_flush) {
            hashtable_init(arp_table);
            time_to_flush = time + ARP_CACHE_LIFE_US;
        }
    }
}
