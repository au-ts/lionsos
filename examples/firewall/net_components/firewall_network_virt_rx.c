/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <os/sddf.h>
#include <sddf/network/constants.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <sddf/util/cache.h>
#include <lions/firewall/arp.h>
#include <lions/firewall/checksum.h>
#include <lions/firewall/config.h>
#include <lions/firewall/ethernet.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".net_virt_rx_config"))) net_virt_rx_config_t config;
__attribute__((__section__(".fw_net_virt_rx_config"))) fw_net_virt_rx_config_t fw_config;

net_queue_handle_t rx_queue_drv;
net_queue_handle_t rx_queue_clients[SDDF_NET_MAX_CLIENTS];

fw_queue_t fw_free_clients[FW_MAX_FW_CLIENTS];

/* Boolean to indicate whether a packet has been enqueued into the driver's free queue during notification handling */
static bool notify_drv;

/* Returns the net client ID of the matching filter if the IP protocol number is
found. ARP requests and responses are handled as a special case. */
static int get_protocol_match(uintptr_t pkt)
{
    uint16_t ethtype = htons(((eth_hdr_t *)pkt)->ethtype);
    for (uint8_t client = 0; client < config.num_clients; client++) {
        /* First check for ethtype match */
        if (fw_config.active_client_ethtypes[client] != ethtype) {
            continue;
        }

        if (ethtype == ETH_TYPE_ARP) {
            /* If ARP traffic, check for opcode match */
            arp_pkt_t *arp = (arp_pkt_t *)(pkt + ARP_PKT_OFFSET);
            if (fw_config.active_client_subtypes[client] == htons(arp->opcode)) {
                return client;
            }
        } else if (ethtype == ETH_TYPE_IP) {
            /* If IPv4 traffic, check for IPv4 protocol match */
            ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt + IPV4_HDR_OFFSET);
            if (fw_config.active_client_subtypes[client] == ip_hdr->protocol) {
                return client;
            }
        }
    }
    
    return -1;
}

static void rx_return(void)
{
    bool reprocess = true;
    bool notify_clients[SDDF_NET_MAX_CLIENTS] = { false };
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue_drv)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue_drv, &buffer);
            assert(!err);

            buffer.io_or_offset = buffer.io_or_offset - config.data.io_addr;
            uintptr_t buffer_vaddr = buffer.io_or_offset + (uintptr_t)config.data.region.vaddr;

            /* Remove additional 4 byte ethernet header from NIC promiscuous mode */
            #if !defined(CONFIG_PLAT_QEMU_ARM_VIRT)
            buffer.len -= 4;
            #endif

            // Cache invalidate after DMA write, so we don't read stale data.
            // This must be performed after the DMA write to avoid reading
            // data that was speculatively fetched before the DMA write.
            //
            // We would invalidate if it worked in usermode. Alas, it
            // does not -- see [1]. The fastest operation that works is a
            // usermode CleanInvalidate (faster than a Invalidate via syscall).
            //
            // [1]: https://developer.arm.com/documentation/ddi0595/2021-06/AArch64-Instructions/DC-IVAC--Data-or-unified-Cache-line-Invalidate-by-VA-to-PoC
            cache_clean_and_invalidate(buffer_vaddr, buffer_vaddr + buffer.len);
            int client = get_protocol_match(buffer_vaddr);
            if (client >= 0) {
                err = net_enqueue_active(&rx_queue_clients[client], buffer);
                assert(!err);
                notify_clients[client] = true;
            } else {
                buffer.io_or_offset = buffer.io_or_offset + config.data.io_addr;
                err = net_enqueue_free(&rx_queue_drv, buffer);
                assert(!err);
                notify_drv = true;
            }
        }

        net_request_signal_active(&rx_queue_drv);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue_drv)) {
            net_cancel_signal_active(&rx_queue_drv);
            reprocess = true;
        }
    }

    for (int client = 0; client < config.num_clients; client++) {
        if (notify_clients[client] && net_require_signal_active(&rx_queue_clients[client])) {
            net_cancel_signal_active(&rx_queue_clients[client]);
            microkit_notify(config.clients[client].conn.id);
        }
    }
}

static void rx_provide(void)
{
    for (int client = 0; client < config.num_clients; client++) {
        bool reprocess = true;
        while (reprocess) {
            while (!net_queue_empty_free(&rx_queue_clients[client])) {
                net_buff_desc_t buffer;
                int err = net_dequeue_free(&rx_queue_clients[client], &buffer);
                assert(!err);
                assert(!(buffer.io_or_offset % NET_BUFFER_SIZE)
                       && (buffer.io_or_offset < NET_BUFFER_SIZE * rx_queue_clients[client].capacity));

                // To avoid having to perform a cache clean here we ensure that
                // the DMA region is only mapped in read only. This avoids the
                // case where pending writes are only written to the buffer
                // memory after DMA has occured.
                buffer.io_or_offset = buffer.io_or_offset + config.data.io_addr;
                err = net_enqueue_free(&rx_queue_drv, buffer);
                assert(!err);
                notify_drv = true;
            }

            net_request_signal_free(&rx_queue_clients[client]);
            reprocess = false;

            if (!net_queue_empty_free(&rx_queue_clients[client])) {
                net_cancel_signal_free(&rx_queue_clients[client]);
                reprocess = true;
            }
        }
    }

    for (int client = 0; client < fw_config.num_free_clients; client++) {
        while (!fw_queue_empty(&fw_free_clients[client])) {
            net_buff_desc_t buffer;
            int err = fw_dequeue(&fw_free_clients[client], &buffer);
            assert(!err);
            assert(!(buffer.io_or_offset % NET_BUFFER_SIZE)
                    && (buffer.io_or_offset < NET_BUFFER_SIZE * fw_free_clients[client].capacity));

            // To avoid having to perform a cache clean here we ensure that
            // the DMA region is only mapped in read only. This avoids the
            // case where pending writes are only written to the buffer
            // memory after DMA has occured.
            buffer.io_or_offset = buffer.io_or_offset + config.data.io_addr;
            err = net_enqueue_free(&rx_queue_drv, buffer);
            assert(!err);
            notify_drv = true;
        }
    }

    if (notify_drv && net_require_signal_free(&rx_queue_drv)) {
        net_cancel_signal_free(&rx_queue_drv);
        microkit_deferred_notify(config.driver.id);
        notify_drv = false;
    }
}

void notified(microkit_channel ch)
{
    rx_return();
    rx_provide();
}

void init(void)
{
    assert(net_config_check_magic((void *)&config));

    /* Set up driver queues */
    net_queue_init(&rx_queue_drv, config.driver.free_queue.vaddr, config.driver.active_queue.vaddr,
                   config.driver.num_buffers);
    net_buffers_init(&rx_queue_drv, config.data.io_addr);

    /* Set up net client queues */
    for (int i = 0; i < config.num_clients; i++) {
        net_queue_init(&rx_queue_clients[i], config.clients[i].conn.free_queue.vaddr,
                       config.clients[i].conn.active_queue.vaddr, config.clients[i].conn.num_buffers);
    }

    /* Set up firewall queues */
    for (int i = 0; i < fw_config.num_free_clients; i++) {
        fw_queue_init(&fw_free_clients[i], fw_config.free_clients[i].queue.vaddr,
            sizeof(net_buff_desc_t), fw_config.free_clients[i].capacity);
    }

    if (net_require_signal_free(&rx_queue_drv)) {
        net_cancel_signal_free(&rx_queue_drv);
        microkit_deferred_notify(config.driver.id);
    }
}
