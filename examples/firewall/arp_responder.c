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

#include "config.h"

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".arp_resources"))) arp_responder_config_t arp_config;

#define NUM_ROUTES 1

serial_queue_handle_t serial_tx_queue_handle;

#define REG_IP 0
#define IPV4_PROTO_LEN 4
#define PADDING_SIZE 10
#define LWIP_IANA_HWTYPE_ETHERNET 1

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

dev_info_t *device_info;

static char *ipaddr_to_string(uint32_t s_addr, char *buf, int buflen)
{
    char inv[3], *rp;
    uint8_t *ap, rem, n, i;
    int len = 0;

    rp = buf;
    ap = (uint8_t *)&s_addr;
    for (n = 0; n < 4; n++) {
        i = 0;
        do {
            rem = *ap % (uint8_t)10;
            *ap /= (uint8_t)10;
            inv[i++] = (char)('0' + rem);
        } while (*ap);
        while (i--) {
            if (len++ >= buflen) {
                return NULL;
            }
            *rp++ = inv[i];
        }
        if (len++ >= buflen) {
            return NULL;
        }
        *rp++ = '.';
        ap++;
    }
    *--rp = 0;
    return buf;
}

static int arp_reply(const uint8_t ethsrc_addr[ETH_HWADDR_LEN],
                     const uint8_t ethdst_addr[ETH_HWADDR_LEN],
                     const uint8_t hwsrc_addr[ETH_HWADDR_LEN], const uint32_t ipsrc_addr,
                     const uint8_t hwdst_addr[ETH_HWADDR_LEN], const uint32_t ipdst_addr)
{
    if (net_queue_empty_free(&tx_queue)) {
        sddf_dprintf("ARP_RESPONDER|LOG: Transmit free queue empty or transmit active queue full. Dropping reply\n");
        return -1;
    }

    net_buff_desc_t buffer;
    int err = net_dequeue_free(&tx_queue, &buffer);
    assert(!err);

    struct arp_packet *reply = (struct arp_packet *)(net_config.tx_data.vaddr + buffer.io_or_offset);
    memcpy(&reply->ethdst_addr, ethdst_addr, ETH_HWADDR_LEN);
    memcpy(&reply->ethsrc_addr, ethsrc_addr, ETH_HWADDR_LEN);

    reply->type = HTONS(ETH_TYPE_ARP);
    reply->hwtype = HTONS(LWIP_IANA_HWTYPE_ETHERNET);
    reply->proto = HTONS(ETH_TYPE_IP);
    reply->hwlen = ETH_HWADDR_LEN;
    reply->protolen = IPV4_PROTO_LEN;
    reply->opcode = HTONS(ETHARP_OPCODE_REPLY);

    memcpy(&reply->hwsrc_addr, hwsrc_addr, ETH_HWADDR_LEN);
    reply->ipsrc_addr = ipsrc_addr;
    memcpy(&reply->hwdst_addr, hwdst_addr, ETH_HWADDR_LEN);
    reply->ipdst_addr = ipdst_addr;
    memset(&reply->padding, 0, 10);

    buffer.len = 56;
    err = net_enqueue_active(&tx_queue, buffer);
    assert(!err);

    return 0;
}

void receive(void)
{
    bool transmitted = false;
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
                if (pkt->opcode == HTONS(ETHARP_OPCODE_REQUEST)) {
                    /* Check it it's for a client */
                    if (pkt->ipdst_addr == arp_config.ip) {
                        /* Send a response */
                        if (!arp_reply(device_info->mac, pkt->ethsrc_addr, device_info->mac, pkt->ipdst_addr,
                                       pkt->hwsrc_addr, pkt->ipsrc_addr)) {
                            transmitted = true;
                        }
                    }
                }
            }

            buffer.len = 0;
            err = net_enqueue_free(&rx_queue, buffer);
            assert(!err);
        }

        net_request_signal_active(&rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue)) {
            net_cancel_signal_active(&rx_queue);
            reprocess = true;
        }
    }

    if (transmitted && net_require_signal_active(&tx_queue)) {
        net_cancel_signal_active(&tx_queue);
        microkit_deferred_notify(net_config.tx.id);
    }
}


void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);
    sddf_dprintf("ARP_RESPONDER|This is the size of the active rx queue: %u\n", (uint16_t)net_queue_length(&rx_queue.active));

    net_queue_init(&tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                   net_config.tx.num_buffers);
    net_buffers_init(&tx_queue, 0);

    device_info = (dev_info_t *)net_config.dev_info.vaddr;
}

void notified(microkit_channel ch)
{
    // For now just check the packets to see if they are an ARP request.
    // Check the header to see if we know the destination IP address.
    // If we do, then we are going to reply to the ARP request with the
    // MAC address of this firewall. Should be the MAC address that we
    // register with the routing client.
    if (ch == net_config.rx.id) {
        receive();
    } else {
        sddf_dprintf("ARP_RESPONDER: Received notification on invalid channel: %d!\n", ch);
    }
}