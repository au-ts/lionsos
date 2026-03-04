/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <os/sddf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/network/constants.h>
#include <sddf/util/cache.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/queue.h>
#include <lions/firewall/ethernet.h>
#include <lions/firewall/ip.h>
#include <lions/firewall/nat_module.h>

__attribute__((__section__(".net_virt_tx_config"))) net_virt_tx_config_t config;
__attribute__((__section__(".fw_net_virt_tx_config"))) fw_net_virt_tx_config_t fw_config;

net_queue_handle_t tx_queue_drv;
net_queue_handle_t tx_queue_clients[SDDF_NET_MAX_CLIENTS];

fw_queue_t fw_free_clients[FW_MAX_FW_CLIENTS];
fw_queue_t fw_active_clients[FW_MAX_FW_CLIENTS];

/* NAT module handles for TCP and UDP */
static nat_module_t nat_tcp_module;
static nat_module_t nat_udp_module;

static int extract_offset_net_client(uintptr_t *phys)
{
    for (int client = 0; client < config.num_clients; client++)
    {
        if (*phys >= config.clients[client].data.io_addr && *phys < config.clients[client].data.io_addr + tx_queue_clients[client].capacity * NET_BUFFER_SIZE)
        {
            *phys = *phys - config.clients[client].data.io_addr;
            return client;
        }
    }
    return -1;
}

static int extract_offset_fw_client(uintptr_t *phys)
{
    for (int client = 0; client < fw_config.num_free_clients; client++)
    {
        if (*phys >= fw_config.free_clients[client].data.io_addr && *phys < fw_config.free_clients[client].data.io_addr + fw_free_clients[client].capacity * NET_BUFFER_SIZE)
        {
            *phys = *phys - fw_config.free_clients[client].data.io_addr;
            return client;
        }
    }
    return -1;
}

static void tx_provide(void)
{
    bool enqueued = false;
    for (int client = 0; client < config.num_clients; client++)
    {
        bool reprocess = true;
        while (reprocess)
        {
            while (!net_queue_empty_active(&tx_queue_clients[client]))
            {
                net_buff_desc_t buffer;
                int err = net_dequeue_active(&tx_queue_clients[client], &buffer);
                assert(!err);

                if (buffer.io_or_offset % NET_BUFFER_SIZE || buffer.io_or_offset >= NET_BUFFER_SIZE * tx_queue_clients[client].capacity)
                {
                    sddf_dprintf("%sVIRT TX LOG: Client provided offset %lx which is not buffer aligned or outside of buffer region\n",
                                 fw_frmt_str[fw_config.interface], buffer.io_or_offset);
                    err = net_enqueue_free(&tx_queue_clients[client], buffer);
                    assert(!err);
                    continue;
                }

                uintptr_t buffer_vaddr = buffer.io_or_offset + (uintptr_t)config.clients[client].data.region.vaddr;

                /* Apply SNAT if enabled */
                if (fw_config.nat_enabled)
                {
                    uint16_t ethtype = htons(((eth_hdr_t *)buffer_vaddr)->ethtype);
                    if (ethtype == ETH_TYPE_IP)
                    {
                        ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(buffer_vaddr + IPV4_HDR_OFFSET);
                        /* All packets in tx_provide are outbound (going to external network) */
                        bool is_inbound = false;

                        int nat_result = NAT_SUCCESS;
                        if (ip_hdr->protocol == IPPROTO_TCP)
                        {
                            nat_result = nat_module_translate(&nat_tcp_module, buffer_vaddr, &buffer, is_inbound);
                        }
                        else if (ip_hdr->protocol == IPPROTO_UDP)
                        {
                            nat_result = nat_module_translate(&nat_udp_module, buffer_vaddr, &buffer, is_inbound);
                        }

                        /* Drop packet if NAT translation fails */
                        if (nat_result != NAT_SUCCESS)
                        {
                            sddf_printf("virt_tx: SNAT translation failed for protocol %u, dropping packet\n",
                                        ip_hdr->protocol);
                            err = net_enqueue_free(&tx_queue_clients[client], buffer);
                            assert(!err);
                            continue;
                        }
                    }
                }

                cache_clean(buffer_vaddr, buffer_vaddr + buffer.len);
                buffer.io_or_offset = buffer.io_or_offset + config.clients[client].data.io_addr;

                err = net_enqueue_active(&tx_queue_drv, buffer);
                assert(!err);
                enqueued = true;
            }

            net_request_signal_active(&tx_queue_clients[client]);
            reprocess = false;

            if (!net_queue_empty_active(&tx_queue_clients[client]))
            {
                net_cancel_signal_active(&tx_queue_clients[client]);
                reprocess = true;
            }
        }
    }

    for (int client = 0; client < fw_config.num_active_clients; client++)
    {
        while (!fw_queue_empty(&fw_active_clients[client]))
        {
            net_buff_desc_t buffer;
            int err = fw_dequeue(&fw_active_clients[client], &buffer);
            assert(!err);

            assert(buffer.io_or_offset % NET_BUFFER_SIZE == 0 &&
                   buffer.io_or_offset < NET_BUFFER_SIZE * fw_active_clients[client].capacity);

            uintptr_t buffer_vaddr = buffer.io_or_offset + (uintptr_t)fw_config.active_clients[client].data.region.vaddr;

            /* Apply SNAT if enabled */
            if (fw_config.nat_enabled)
            {
                uint16_t ethtype = htons(((eth_hdr_t *)buffer_vaddr)->ethtype);
                if (ethtype == ETH_TYPE_IP)
                {
                    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(buffer_vaddr + IPV4_HDR_OFFSET);
                    /* All packets in tx_provide are outbound (going to external network) */
                    bool is_inbound = false;

                    int nat_result = NAT_SUCCESS;
                    if (ip_hdr->protocol == IPPROTO_TCP)
                    {
                        nat_result = nat_module_translate(&nat_tcp_module, buffer_vaddr, &buffer, is_inbound);
                    }
                    else if (ip_hdr->protocol == IPPROTO_UDP)
                    {
                        nat_result = nat_module_translate(&nat_udp_module, buffer_vaddr, &buffer, is_inbound);
                    }

                    /* Drop packet if NAT translation fails - return to firewall client */
                    if (nat_result != NAT_SUCCESS)
                    {
                        sddf_printf("virt_tx: SNAT translation failed for protocol %u, returning to firewall\n",
                                    ip_hdr->protocol);
                        err = fw_enqueue(&fw_free_clients[client], &buffer);
                        assert(!err);
                        continue;
                    }
                }
            }

            cache_clean(buffer_vaddr, buffer_vaddr + buffer.len);
            buffer.io_or_offset = buffer.io_or_offset + fw_config.active_clients[client].data.io_addr;

            err = net_enqueue_active(&tx_queue_drv, buffer);
            assert(!err);
            enqueued = true;
        }
    }

    if (enqueued && net_require_signal_active(&tx_queue_drv))
    {
        net_cancel_signal_active(&tx_queue_drv);
        microkit_deferred_notify(config.driver.id);
    }
}

static void tx_return(void)
{
    bool reprocess = true;
    bool notify_net_clients[SDDF_NET_MAX_CLIENTS] = {false};
    bool notify_fw_clients[SDDF_NET_MAX_CLIENTS] = {false};
    while (reprocess)
    {
        while (!net_queue_empty_free(&tx_queue_drv))
        {
            net_buff_desc_t buffer;
            int err = net_dequeue_free(&tx_queue_drv, &buffer);
            assert(!err);

            int client = extract_offset_net_client(&buffer.io_or_offset);
            if (client >= 0)
            {
                err = net_enqueue_free(&tx_queue_clients[client], buffer);
                assert(!err);
                notify_net_clients[client] = true;
                continue;
            }
            client = extract_offset_fw_client(&buffer.io_or_offset);
            assert(client >= 0);

            err = fw_enqueue(&fw_free_clients[client], &buffer);
            assert(!err);
            notify_fw_clients[client] = true;
        }

        net_request_signal_free(&tx_queue_drv);
        reprocess = false;

        if (!net_queue_empty_free(&tx_queue_drv))
        {
            net_cancel_signal_free(&tx_queue_drv);
            reprocess = true;
        }
    }

    for (int client = 0; client < config.num_clients; client++)
    {
        if (notify_net_clients[client] && net_require_signal_free(&tx_queue_clients[client]))
        {
            net_cancel_signal_free(&tx_queue_clients[client]);
            microkit_notify(config.clients[client].conn.id);
        }
    }

    for (int client = 0; client < fw_config.num_free_clients; client++)
    {
        if (notify_fw_clients[client])
        {
            microkit_notify(fw_config.free_clients[client].conn.ch);
        }
    }
}

void notified(microkit_channel ch)
{
    tx_return();
    tx_provide();
}

void init(void)
{
    assert(net_config_check_magic(&config));

    /* Set up driver queues */
    net_queue_init(&tx_queue_drv, config.driver.free_queue.vaddr, config.driver.active_queue.vaddr,
                   config.driver.num_buffers);

    for (int i = 0; i < config.num_clients; i++)
    {
        net_queue_init(&tx_queue_clients[i], config.clients[i].conn.free_queue.vaddr,
                       config.clients[i].conn.active_queue.vaddr, config.clients[i].conn.num_buffers);
    }

    /* Set up firewall queues */
    for (int i = 0; i < fw_config.num_active_clients; i++)
    {
        fw_queue_init(&fw_active_clients[i], fw_config.active_clients[i].conn.queue.vaddr,
                      sizeof(net_buff_desc_t), fw_config.active_clients[i].conn.capacity);
    }

    for (int i = 0; i < fw_config.num_free_clients; i++)
    {
        fw_queue_init(&fw_free_clients[i], fw_config.free_clients[i].conn.queue.vaddr,
                      sizeof(net_buff_desc_t), fw_config.free_clients[i].capacity);
    }

    /* Initialize NAT modules if enabled */
    if (fw_config.nat_enabled)
    {
        fw_nat_webserver_state_t *webserver_state = (fw_nat_webserver_state_t *)fw_config.webserver_state.vaddr;

        for (int i = 0; i < fw_config.num_nat_configs; i++)
        {
            fw_virt_rx_nat_config_t *nat_cfg = &fw_config.nat_configs[i];

            if (!nat_cfg->enabled)
            {
                continue;
            }

            /* Get the interface configuration for this NAT config */
            fw_nat_interface_config_t *interface_config = &nat_cfg->interface_config;
            fw_nat_port_table_t *port_table = (fw_nat_port_table_t *)interface_config->port_table.vaddr;

            /* Determine protocol-specific offsets */
            size_t src_port_off, dst_port_off, check_off;
            bool check_enabled;

            if (nat_cfg->protocol == IPPROTO_TCP)
            {
                src_port_off = TCP_SRC_PORT_OFF;
                dst_port_off = TCP_DST_PORT_OFF;
                check_off = TCP_CHECK_OFF;
                check_enabled = true;

                /* Initialize TCP NAT module */
                int result = nat_module_init(&nat_tcp_module,
                                             fw_config.interface,
                                             IPPROTO_TCP,
                                             interface_config,
                                             port_table,
                                             webserver_state,
                                             src_port_off,
                                             dst_port_off,
                                             check_off,
                                             check_enabled);
                assert(result == NAT_SUCCESS);
            }
            else if (nat_cfg->protocol == IPPROTO_UDP)
            {
                src_port_off = UDP_SRC_PORT_OFF;
                dst_port_off = UDP_DST_PORT_OFF;
                check_off = UDP_CHECK_OFF;
                check_enabled = true;

                /* Initialize UDP NAT module */
                int result = nat_module_init(&nat_udp_module,
                                             fw_config.interface,
                                             IPPROTO_UDP,
                                             interface_config,
                                             port_table,
                                             webserver_state,
                                             src_port_off,
                                             dst_port_off,
                                             check_off,
                                             check_enabled);
                assert(result == NAT_SUCCESS);
            }
        }
    }

    tx_provide();
}