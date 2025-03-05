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
#include <string.h>

#include "firewall_arp.h"
#include "hashmap.h"
#include "config.h"

__attribute__((__section__(".arp_resources"))) arp_requester_config_t arp_config;

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

#define MAX_ARP_CACHE 64
#define LWIP_IANA_HWTYPE_ETHERNET 1

uint8_t broadcast_mac_addr[ETH_HWADDR_LEN]= {0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t mac_addr[ETH_HWADDR_LEN]= {0x52,0x54,0x01,0x00,0x00,0x78};

// This component needs to be connected to BOTH rx and tx of this
// network subsystem.

hashtable_t *arp_table;
net_queue_handle_t virt_tx_queue;
net_queue_handle_t virt_rx_queue;

arp_entry_t arp_cache[MAX_ARP_CACHE];

/* This queue will hold all the ARP requests/responses that are needed by the
packets in the arp_waiting queue. */
arp_queue_handle_t *arp_query;

dev_info_t *device_info;

void process_requests()
{
    bool transmitted = false;
    // Loop through and generate ARP requests.
    while (!arp_queue_empty_request(arp_query) && !net_queue_empty_free(&virt_tx_queue)) {
        arp_request_t request;
        int err = arp_dequeue_request(arp_query, &request);
        assert(!err);
        if (request.valid != true) {
            sddf_dprintf("ARP_REQUESTER|ARP request was invalid!\n");
        }

        // Generate the ARP request here.
        net_buff_desc_t buffer;
        err = net_dequeue_free(&virt_tx_queue, &buffer);

        struct arp_packet *pkt = (struct arp_packet *)(net_config.tx_data.vaddr + buffer.io_or_offset);
        // Set the destination MAC address as the broadcast MAC address.
        sddf_memcpy(&pkt->ethdst_addr, broadcast_mac_addr, ETH_HWADDR_LEN);
        sddf_memcpy(&pkt->ethsrc_addr, device_info->mac, ETH_HWADDR_LEN);
        sddf_memcpy(&pkt->hwsrc_addr, device_info->mac, ETH_HWADDR_LEN);

        pkt->type = HTONS(ETH_TYPE_ARP);
        pkt->hwtype = HTONS(ETH_HWTYPE);
        pkt->proto = HTONS(ETH_TYPE_IP);
        pkt->hwlen = ETH_HWADDR_LEN;
        pkt->protolen = IPV4_PROTO_LEN;
        pkt->opcode = HTONS(ETHARP_OPCODE_REQUEST);

        // Memset the hardware src addr to 0 for ARP requests.
        sddf_memset(&pkt->hwdst_addr, 0, ETH_HWADDR_LEN);

        pkt->ipdst_addr = request.ip_addr;

        pkt->ipsrc_addr = arp_config.ip;
        sddf_memset(&pkt->padding, 0, 10);

        buffer.len = 56;
        err = net_enqueue_active(&virt_tx_queue, buffer);
        assert(!err);
        transmitted = true;
        request.valid = false;
    }
    if (transmitted && net_require_signal_active(&virt_tx_queue)) {
        net_cancel_signal_active(&virt_tx_queue);
        microkit_deferred_notify(net_config.tx.id);
    }
}

void process_responses()
{
    bool signal = false;
    while (!net_queue_empty_active(&virt_rx_queue)) {
        net_buff_desc_t buffer;
        int err = net_dequeue_active(&virt_rx_queue, &buffer);
        assert(!err);

        /* Check if packet is an ARP request */
        struct ethernet_header *ethhdr = (struct ethernet_header *)(net_config.rx_data.vaddr + buffer.io_or_offset);
        if (ethhdr->type == HTONS(ETH_TYPE_ARP)) {
            struct arp_packet *pkt = (struct arp_packet *)ethhdr;
            /* Check if it's a probe, ignore announcements */
            if (pkt->opcode == HTONS(ETHARP_OPCODE_REPLY)) {
                // Decode the response data, and place it into the ARP response queue for the
                // routing component.
                arp_request_t resp = {0};
                resp.ip_addr = pkt->ipsrc_addr;
                sddf_memcpy(resp.mac_addr, pkt->hwsrc_addr, sizeof(uint8_t) * ETH_HWADDR_LEN);
                resp.valid = true;
                arp_enqueue_response(arp_query, &resp);
                // We are also going to add the ip -> mac mapping to the ARP table.
                arp_entry_t entry = {0};
                sddf_memcpy(entry.mac_addr, resp.mac_addr, sizeof(uint8_t) * ETH_HWADDR_LEN);
                entry.valid = true;
                hashtable_insert(arp_table, (uint32_t) resp.ip_addr, &entry);
                signal = true;
            }
        }

        buffer.len = 0;
        err = net_enqueue_free(&virt_rx_queue, buffer);
        assert(!err);

        net_request_signal_active(&virt_rx_queue);
    }

    microkit_deferred_notify(arp_config.router.id);
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&virt_rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);
    net_queue_init(&virt_tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                   net_config.tx.num_buffers);
    net_buffers_init(&virt_tx_queue, 0);

    arp_query = (arp_queue_handle_t *) arp_config.router.arp_queue.vaddr;
    /* This hashtable will have been initialised by the router component. */
    arp_table = (hashtable_t *) arp_config.router.arp_cache.vaddr;
    device_info = (dev_info_t *) net_config.dev_info.vaddr;
}

void notified(microkit_channel ch)
{
    if (ch == arp_config.router.id) {
        process_requests();
    } if (ch == net_config.rx.id) {
        process_responses();
    }
}
