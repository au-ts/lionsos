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
#include <lions/firewall/arp_queue.h>
#include <lions/firewall/config.h>
#include <lions/firewall/protocols.h>
#include <string.h>

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".firewall_arp_requester_config"))) firewall_arp_requester_config_t arp_config;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

/* Queues hold ARP requests/responses for router and webserver */
arp_queue_handle_t *arp_queues[FIREWALL_NUM_ARP_REQUESTER_CLIENTS];

/* ARP table caches ARP request responses */
arp_table_t arp_table;

static bool notify_client[FIREWALL_NUM_ARP_REQUESTER_CLIENTS] = {false};
void process_requests()
{
    bool transmitted = false;
    for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
        while (!arp_queue_empty_request(arp_queues[client]) && !net_queue_empty_free(&tx_queue)) {
            arp_request_t request;
            int err = arp_dequeue_request(arp_queues[client], &request);
            assert(!err);

            /* Check if an arp entry already exists */
            arp_entry_t *entry = arp_table_find_entry(&arp_table, request.ip);
            if (entry != NULL && !entry->pending) {
                /* Reply immediately */
                arp_enqueue_response(arp_queues[client], arp_response_from_entry(entry));
                notify_client[client] = true;
                continue;
            } else if (entry != NULL && entry->pending) {
                /* Notify client upon response for existing ARP request */
                entry->client[client] = true;
                continue;
            }
            

            /* Generate ARP request */
            net_buff_desc_t buffer = {};
            err = net_dequeue_free(&tx_queue, &buffer);
            assert(!err);

            arp_packet_t *pkt = (arp_packet_t *)(net_config.tx_data.vaddr + buffer.io_or_offset);

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
            pkt->ipdst_addr = request.ip;
            pkt->ipsrc_addr = arp_config.ip;
            memset(&pkt->padding, 0, 10);

            if (FIREWALL_DEBUG_OUTPUT) {
                sddf_printf("MAC[5] = %x | ARP requester processing client %u request for ip %u\n", arp_config.mac_addr[5], client, request.ip);
            }

            buffer.len = 56;
            err = net_enqueue_active(&tx_queue, buffer);
            assert(!err);

            /* Create arp entry for request to store associated client */
            arp_error_t arp_err = arp_table_add_entry(&arp_table, true, request.ip, NULL, client);
            if (arp_err == ARP_ERR_FULL) {
                sddf_dprintf("ARP REQUESTER|LOG: Arp cache full, cannot enqueue entry!\n");
            }
            transmitted = true;
        }
    }

    if (transmitted && net_require_signal_active(&tx_queue)) {
        net_cancel_signal_active(&tx_queue);
        microkit_deferred_notify(net_config.tx.id);
    }
}

void process_responses()
{
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
                    arp_entry_t *entry = arp_table_find_entry(&arp_table, pkt->ipsrc_addr);
                    if (entry != NULL) {
                        /* This was a response to a request we sent, update entry */
                        entry->pending = false;
                        memcpy(&entry->mac_addr, &pkt->hwsrc_addr, ETH_HWADDR_LEN);
                        entry->reachable = true;
                        
                        /* Send to clients */
                        for (uint8_t client = 0; client < FIREWALL_NUM_ARP_REQUESTER_CLIENTS; client++) {
                            if (entry->client[client]) {
                                arp_enqueue_response(arp_queues[client], arp_response_from_entry(entry));
                                notify_client[client] = true;

                                if (FIREWALL_DEBUG_OUTPUT) {
                                    sddf_printf("MAC[5] = %x | ARP requester received response for client %u, ip %u. MAC[0] = %x, MAC[5] = %x\n", arp_config.mac_addr[5], client, pkt->ipsrc_addr, pkt->hwsrc_addr[0], pkt->hwsrc_addr[5]);
                                }
                            }
                        }

                    } else {
                        /* Create a new entry */
                        arp_error_t arp_err = arp_table_add_entry(&arp_table, false, pkt->ipsrc_addr, pkt->hwsrc_addr, arp_config.num_arp_clients + 1);
                        if (arp_err == ARP_ERR_FULL) {
                            sddf_dprintf("ARP REQUESTER|LOG: Arp cache full, cannot enqueue entry!\n");
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

    for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
        arp_queues[client] = (arp_queue_handle_t *) arp_config.clients[client].queue.vaddr;
        arp_handle_init(arp_queues[client], arp_config.clients[client].capacity);
    }

    arp_table_init(&arp_table, (arp_entry_t *)arp_config.arp_cache.vaddr, arp_config.arp_cache_capacity);
}

void notified(microkit_channel ch)
{
    if (ch == arp_config.clients[0].ch || ch == arp_config.clients[1].ch) {
        process_requests();
    } if (ch == net_config.rx.id) {
        process_responses();
    }

    for (uint8_t client = 0; client < arp_config.num_arp_clients; client++) {
        if (notify_client[client]) {
            microkit_notify(arp_config.clients[client].ch);
        }
    }
}
