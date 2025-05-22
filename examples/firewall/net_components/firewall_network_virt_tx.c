/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <os/sddf.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/util/cache.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/queue.h>

__attribute__((__section__(".net_virt_tx_config"))) net_virt_tx_config_t config;

__attribute__((__section__(".fw_net_virt_tx_config"))) fw_net_virt_tx_config_t firewall_config;

typedef struct state {
    net_queue_handle_t tx_queue_drv;
    net_queue_handle_t tx_queue_clients[SDDF_NET_MAX_CLIENTS];
    fw_queue_handle_t firewall_free_clients[FW_MAX_FW_CLIENTS];
    fw_queue_handle_t firewall_active_clients[FW_MAX_FW_CLIENTS];
} state_t;

state_t state;

static int extract_offset_net_client(uintptr_t *phys)
{
    for (int client = 0; client < config.num_clients; client++) {
        if (*phys >= config.clients[client].data.io_addr
            && *phys
                   < config.clients[client].data.io_addr + state.tx_queue_clients[client].capacity * NET_BUFFER_SIZE) {
            *phys = *phys - config.clients[client].data.io_addr;
            return client;
        }
    }
    return -1;
}

static int extract_offset_firewall_client(uintptr_t *phys)
{
    for (int client = 0; client < firewall_config.num_free_clients; client++) {
        if (*phys >= firewall_config.free_clients[client].data.io_addr
            && *phys
                   < firewall_config.free_clients[client].data.io_addr + state.firewall_free_clients[client].capacity * NET_BUFFER_SIZE) {
            *phys = *phys - firewall_config.free_clients[client].data.io_addr;
            return client;
        }
    }
    return -1;
}

void tx_provide(void)
{
    bool enqueued = false;
    for (int client = 0; client < config.num_clients; client++) {
        bool reprocess = true;
        while (reprocess) {
            while (!net_queue_empty_active(&state.tx_queue_clients[client])) {
                net_buff_desc_t buffer;
                int err = net_dequeue_active(&state.tx_queue_clients[client], &buffer);
                assert(!err);

                if (buffer.io_or_offset % NET_BUFFER_SIZE
                    || buffer.io_or_offset >= NET_BUFFER_SIZE * state.tx_queue_clients[client].capacity) {
                    sddf_dprintf("%sVIRT TX LOG: Client provided offset %lx which is not buffer aligned or outside of buffer region\n",
                                 fw_frmt_str[firewall_config.interface],
                                 buffer.io_or_offset);
                    err = net_enqueue_free(&state.tx_queue_clients[client], buffer);
                    assert(!err);
                    continue;
                }

                uintptr_t buffer_vaddr = buffer.io_or_offset + (uintptr_t)config.clients[client].data.region.vaddr;
                cache_clean(buffer_vaddr, buffer_vaddr + buffer.len);
                buffer.io_or_offset = buffer.io_or_offset + config.clients[client].data.io_addr;

                err = net_enqueue_active(&state.tx_queue_drv, buffer);
                assert(!err);
                enqueued = true;
            }

            net_request_signal_active(&state.tx_queue_clients[client]);
            reprocess = false;

            if (!net_queue_empty_active(&state.tx_queue_clients[client])) {
                net_cancel_signal_active(&state.tx_queue_clients[client]);
                reprocess = true;
            }
        }
    }

    for (int client = 0; client < firewall_config.num_active_clients; client++) {
        while (!fw_queue_empty(&state.firewall_active_clients[client])) {
            fw_buff_desc_t buffer;
            int err = fw_dequeue(&state.firewall_active_clients[client], &buffer);
            assert(!err);

            assert(buffer.io_or_offset % NET_BUFFER_SIZE == 0 && 
                   buffer.io_or_offset < NET_BUFFER_SIZE * state.firewall_active_clients[client].capacity);

            uintptr_t buffer_vaddr = buffer.io_or_offset + (uintptr_t)firewall_config.active_clients[client].data.region.vaddr;
            cache_clean(buffer_vaddr, buffer_vaddr + buffer.len);
            buffer.io_or_offset = buffer.io_or_offset + firewall_config.active_clients[client].data.io_addr;

            err = net_enqueue_active(&state.tx_queue_drv, fw_to_net_desc(buffer));
            assert(!err);
            enqueued = true;
        }
    }

    if (enqueued && net_require_signal_active(&state.tx_queue_drv)) {
        net_cancel_signal_active(&state.tx_queue_drv);
        microkit_deferred_notify(config.driver.id);
    }
}

void tx_return(void)
{
    bool reprocess = true;
    bool notify_net_clients[SDDF_NET_MAX_CLIENTS] = { false };
    bool notify_firewall_clients[SDDF_NET_MAX_CLIENTS] = { false };
    while (reprocess) {
        while (!net_queue_empty_free(&state.tx_queue_drv)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_free(&state.tx_queue_drv, &buffer);
            assert(!err);

            int client = extract_offset_net_client(&buffer.io_or_offset);
            if (client >= 0) {


                err = net_enqueue_free(&state.tx_queue_clients[client], buffer);
                assert(!err);
                notify_net_clients[client] = true;
                continue;
            }
            client = extract_offset_firewall_client(&buffer.io_or_offset);
            assert(client >= 0);


            err = fw_enqueue(&state.firewall_free_clients[client], net_fw_desc(buffer));
            assert(!err);
            notify_firewall_clients[client] = true;
        }

        net_request_signal_free(&state.tx_queue_drv);
        reprocess = false;

        if (!net_queue_empty_free(&state.tx_queue_drv)) {
            net_cancel_signal_free(&state.tx_queue_drv);
            reprocess = true;
        }
    }

    for (int client = 0; client < config.num_clients; client++) {
        if (notify_net_clients[client] && net_require_signal_free(&state.tx_queue_clients[client])) {
            net_cancel_signal_free(&state.tx_queue_clients[client]);
            microkit_notify(config.clients[client].conn.id);
        }
    }

    for (int client = 0; client < firewall_config.num_free_clients; client++) {
        if (notify_firewall_clients[client]) {
            microkit_notify(firewall_config.free_clients[client].conn.ch);
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
    net_queue_init(&state.tx_queue_drv, config.driver.free_queue.vaddr, config.driver.active_queue.vaddr,
                   config.driver.num_buffers);

    for (int i = 0; i < config.num_clients; i++) {
        net_queue_init(&state.tx_queue_clients[i], config.clients[i].conn.free_queue.vaddr,
                       config.clients[i].conn.active_queue.vaddr, config.clients[i].conn.num_buffers);
    }

    /* Set up firewall queues */
    for (int i = 0; i < firewall_config.num_active_clients; i++) {
        fw_queue_init(&state.firewall_active_clients[i], firewall_config.active_clients[i].conn.queue.vaddr,
                            firewall_config.active_clients[i].conn.capacity);
    }

    for (int i = 0; i < firewall_config.num_free_clients; i++) {
        fw_queue_init(&state.firewall_free_clients[i], firewall_config.free_clients[i].conn.queue.vaddr,
                            firewall_config.free_clients[i].conn.capacity);
    }
    tx_provide();
}
