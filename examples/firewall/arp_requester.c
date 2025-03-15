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
#include <firewall_arp.h>
#include <hashmap.h>
#include <protocols.h>
#include <string.h>

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".firewall_arp_requester_config"))) firewall_arp_requester_config_t arp_config;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

/* This queue holds ARP requests/responses for outgoing packets in the router */
arp_queue_handle_t *arp_queue;

/* ARP table holds all known ARP entries */
hashtable_t *arp_table;

void process_requests()
{
    bool transmitted = false;
    while (!arp_queue_empty_request(arp_queue) && !net_queue_empty_free(&tx_queue)) {
        arp_request_t request;
        int err = arp_dequeue_request(arp_queue, &request);
        assert(!err);

        if (!request.valid) {
            sddf_dprintf("ARP_REQUESTER|LOG: Dequeued ARP request was invalid!\n");
        }

        /* Generate ARP request */
        net_buff_desc_t buffer = {};
        err = net_dequeue_free(&tx_queue, &buffer);
        assert(!err);

        struct arp_packet *pkt = (struct arp_packet *)(net_config.tx_data.vaddr + buffer.io_or_offset);

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
    bool enqueued = false;
    bool returned = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            /* Check if packet is an ARP request */
            struct ethernet_header *ethhdr = (struct ethernet_header *)(net_config.rx_data.vaddr + buffer.io_or_offset);
            if (ethhdr->type == HTONS(ETH_TYPE_ARP)) {
                struct arp_packet *pkt = (struct arp_packet *)ethhdr;

                /* Check if it's a probe, ignore announcements */
                if (pkt->opcode == HTONS(ETHARP_OPCODE_REPLY)) {

                    /* Place response in queue for router */
                    arp_enqueue_response(arp_queue, pkt->ipsrc_addr, pkt->hwsrc_addr, true);

                    if (FIREWALL_DEBUG_OUTPUT) {
                        sddf_printf("MAC[5] = %x | ARP requester processing response for ip %u and MAC[0] = %x, MAC[5] = %x\n", arp_config.mac_addr[5], pkt->ipsrc_addr, pkt->hwsrc_addr[0], pkt->hwsrc_addr[5]);
                    }

                    /* Add the ip -> mac mapping to the ARP table */
                    arp_entry_t entry = {0};
                    memcpy(&entry.mac_addr, &pkt->hwsrc_addr, ETH_HWADDR_LEN);
                    entry.valid = true;
                    err = hashtable_insert(arp_table, (uint32_t) pkt->ipsrc_addr, &entry);
                    if (err) {
                        sddf_dprintf("ARP_REQUESTER|ERR: Hash table full, failed to insert!\n");
                    }

                    enqueued = true;
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
        microkit_notify(net_config.rx.id);
    }

    if (enqueued) {
        microkit_deferred_notify(arp_config.router.arp_queue.ch);
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

    arp_queue = (arp_queue_handle_t *) arp_config.router.arp_queue.queue.vaddr;
    arp_handle_init(arp_queue, arp_config.router.arp_queue.capacity);

    arp_table = (hashtable_t *) arp_config.router.arp_cache.vaddr;
}

void notified(microkit_channel ch)
{
    if (ch == arp_config.router.arp_queue.ch) {
        process_requests();
    } if (ch == net_config.rx.id) {
        process_responses();
    }
}
