/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <os/sddf.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <lions/firewall/arp.h>
#include <lions/firewall/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/ethernet.h>

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fw_arp_responder_config"))) fw_arp_responder_config_t arp_config;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

static int arp_reply(const uint8_t ethsrc_addr[ETH_HWADDR_LEN],
                     const uint8_t ethdst_addr[ETH_HWADDR_LEN],
                     const uint8_t hwsrc_addr[ETH_HWADDR_LEN], const uint32_t ipsrc_addr,
                     const uint8_t hwdst_addr[ETH_HWADDR_LEN], const uint32_t ipdst_addr)
{
    if (net_queue_empty_free(&tx_queue)) {
        sddf_dprintf("%sARP_RESPONDER LOG: Transmit free queue empty. Dropping reply\n",
        fw_frmt_str[arp_config.interface]);
        return -1;
    }

    net_buff_desc_t buffer;
    int err = net_dequeue_free(&tx_queue, &buffer);
    assert(!err);

    uintptr_t pkt_vaddr = (uintptr_t)(net_config.tx_data.vaddr + buffer.io_or_offset);

    eth_hdr_t *eth_hdr = (eth_hdr_t *)pkt_vaddr;
    memcpy(&eth_hdr->ethdst_addr, ethdst_addr, ETH_HWADDR_LEN);
    memcpy(&eth_hdr->ethsrc_addr, ethsrc_addr, ETH_HWADDR_LEN);
    eth_hdr->ethtype = htons(ETH_TYPE_ARP);

    arp_pkt_t *reply = (arp_pkt_t *)(pkt_vaddr + ARP_PKT_OFFSET);
    reply->hwtype = htons(ARP_HWTYPE_ETH);
    reply->protocol = htons(ETH_TYPE_IP);
    reply->hwlen = ETH_HWADDR_LEN;
    reply->protolen = ARP_PROTO_LEN_IPV4;
    reply->opcode = htons(ARP_ETH_OPCODE_REPLY);

    memcpy(&reply->hwsrc_addr, hwsrc_addr, ETH_HWADDR_LEN);
    reply->ipsrc_addr = ipsrc_addr;
    memcpy(&reply->hwdst_addr, hwdst_addr, ETH_HWADDR_LEN);
    reply->ipdst_addr = ipdst_addr;

    buffer.len = ARP_PKT_LEN;
    err = net_enqueue_active(&tx_queue, buffer);
    assert(!err);

    return 0;
}

static void receive(void)
{
    bool transmitted = false;
    bool returned = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            uintptr_t pkt_vaddr = (uintptr_t)(net_config.rx_data.vaddr + buffer.io_or_offset);

            /* Check if packet is an ARP request */
            eth_hdr_t *eth_hdr = (eth_hdr_t *)pkt_vaddr;
            if (eth_hdr->ethtype == htons(ETH_TYPE_ARP)) {
                arp_pkt_t *arp_pkt = (arp_pkt_t *)(pkt_vaddr + ARP_PKT_OFFSET);
                /* Check if it's a probe, ignore announcements */
                if (arp_pkt->opcode == htons(ARP_ETH_OPCODE_REQUEST)) {
                    /* Check the destination IP address */
                    if (arp_pkt->ipdst_addr == arp_config.ip) {

                        if (FW_DEBUG_OUTPUT) {
                            sddf_printf("%sARP Responder replying for ip %s\n", fw_frmt_str[arp_config.interface],
                                ipaddr_to_string(arp_pkt->ipdst_addr, ip_addr_buf0));
                        }

                        /* Reply with the MAC of the firewall */
                        if (!arp_reply(arp_config.mac_addr, eth_hdr->ethsrc_addr, arp_config.mac_addr,
                                       arp_config.ip, arp_pkt->hwsrc_addr, arp_pkt->ipsrc_addr)) {
                            transmitted = true;
                        }
                    }
                }
            }

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

    if (transmitted && net_require_signal_active(&tx_queue)) {
        net_cancel_signal_active(&tx_queue);
        microkit_deferred_notify(net_config.tx.id);
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
}

void notified(microkit_channel ch)
{
    if (ch == net_config.rx.id) {
        receive();
    }
}
