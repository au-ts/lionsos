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
#include <lions/firewall/hashmap.h>
#include <lions/firewall/protocols.h>
#include <string.h>

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".firewall_arp_requester_config"))) firewall_arp_requester_config_t arp_config;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

/* Queues hold ARP requests/responses for router */
arp_queue_handle_t *arp_queue;

/* ARP table caches ARP request responses */
hashtable_t *arp_table;

void process_requests()
{
    bool transmitted = false;
    while (!arp_queue_empty_request(arp_queue) && !net_queue_empty_free(&tx_queue)) {
        arp_request_t request;
        int err = arp_dequeue_request(arp_queue, &request);
        assert(!err && request.valid);

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
        pkt->ipdst_addr = request.ip_addr;
        pkt->ipsrc_addr = arp_config.ip;
        memset(&pkt->padding, 0, 10);

        if (FIREWALL_DEBUG_OUTPUT) {
            sddf_printf("MAC[5] = %x | ARP requester processing request for ip %u\n", arp_config.mac_addr[5], request.ip_addr);
        }

        buffer.len = 56;
        err = net_enqueue_active(&tx_queue, buffer);
        assert(!err);

        /* Create arp entry for request */
        arp_entry_t entry = {0};
        entry.valid = false;
        err = hashtable_insert(arp_table, request.ip_addr, &entry);

        transmitted = true;
        request.valid = false;
    }

    if (transmitted && net_require_signal_active(&tx_queue)) {
        net_cancel_signal_active(&tx_queue);
        microkit_deferred_notify(net_config.tx.id);
    }
}

void process_responses()
{
    bool notify_client = false;
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
                    arp_entry_t *entry = NULL;
                    int found = hashtable_search(arp_table, pkt->ipsrc_addr, entry);
                    if (found) {
                        /* This was a response to a request we sent, update entry */
                        memcpy(&entry->mac_addr, &pkt->hwsrc_addr, ETH_HWADDR_LEN);

                        if (FIREWALL_DEBUG_OUTPUT) {
                            sddf_printf("MAC[5] = %x | ARP requester received response for ip %u. MAC[0] = %x, MAC[5] = %x\n", arp_config.mac_addr[5], pkt->ipsrc_addr, pkt->hwsrc_addr[0], pkt->hwsrc_addr[5]);
                        }
                        
                        /* Send to router */
                        arp_enqueue_response(arp_queue, pkt->ipsrc_addr, entry->mac_addr, true);
                        notify_client = true;
                    } else {
                        /* Create a new entry */
                        arp_entry_t entry = {0};
                        memcpy(&entry.mac_addr, &pkt->hwsrc_addr, ETH_HWADDR_LEN);
                        entry.valid = true;
                        err = hashtable_insert(arp_table, pkt->ipsrc_addr, &entry);
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

    if (notify_client) {
        microkit_notify(arp_config.router.ch);
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

    arp_queue = (arp_queue_handle_t *) arp_config.router.queue.vaddr;
    arp_handle_init(arp_queue, arp_config.router.capacity);

    arp_table = (hashtable_t *) arp_config.arp_cache.vaddr;
}

void notified(microkit_channel ch)
{
    if (ch == arp_config.router.ch) {
        process_requests();
    } if (ch == net_config.rx.id) {
        process_responses();
    }
}
