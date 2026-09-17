/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
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
__attribute__((__section__(".fw_net_virt_tx_config"))) fw_net_virt_tx_config_t fw_config;

net_queue_handle_t tx_queue_drv;
net_queue_handle_t tx_queue_clients[SDDF_NET_MAX_CLIENTS];

fw_queue_t fw_free_clients[FW_MAX_FW_CLIENTS];
fw_queue_t fw_active_clients[FW_MAX_FW_CLIENTS];

uint32_t num_queued_to_driver;

/* Alternate which client group goes first each pass*/
bool net_clients_go_first = true;

static inline bool driver_queue_has_space(void)
{
    return num_queued_to_driver < tx_queue_drv.capacity;
}

static int extract_offset_net_client(uintptr_t *phys)
{
    for (int client = 0; client < config.num_clients; client++) {
        if (*phys >= config.clients[client].regions[0].data.io_addr
            && *phys < config.clients[client].regions[0].data.io_addr + tx_queue_clients[client].capacity * NET_BUFFER_SIZE) {
            *phys = *phys - config.clients[client].regions[0].data.io_addr;
            return client;
        }
    }
    return -1;
}

static int extract_offset_fw_client(uintptr_t *phys)
{
    for (int client = 0; client < fw_config.num_free_clients; client++) {
        if (*phys >= fw_config.free_clients[client].data.io_addr
            && *phys
                   < fw_config.free_clients[client].data.io_addr + fw_free_clients[client].capacity * NET_BUFFER_SIZE) {
            *phys = *phys - fw_config.free_clients[client].data.io_addr;
            return client;
        }
    }
    return -1;
}

/* Forwards one buffer for this client, if it has one. The caller must have
already checked driver_queue_has_space(). */
static bool tx_provide_net_client(int client)
{
    while (!net_queue_empty_active(&tx_queue_clients[client])) {
        net_buff_desc_t buffer;
        int err = net_dequeue_active(&tx_queue_clients[client], &buffer);
        assert(!err);

        if (buffer.io_or_offset % NET_BUFFER_SIZE
            || buffer.io_or_offset >= NET_BUFFER_SIZE * tx_queue_clients[client].capacity) {
            sddf_dprintf("VIRT TX LOG, Interface %u: Client provided offset %lx which is not buffer aligned or "
                         "outside of buffer region\n",
                         fw_config.interface, buffer.io_or_offset);
            err = net_enqueue_free(&tx_queue_clients[client], buffer);
            assert(!err);
            continue;
        }

        uintptr_t buffer_vaddr = buffer.io_or_offset + (uintptr_t)config.clients[client].regions[0].data.region.vaddr;
        cache_clean(buffer_vaddr, buffer_vaddr + buffer.len);
        buffer.io_or_offset = buffer.io_or_offset + config.clients[client].regions[0].data.io_addr;

        err = net_enqueue_active(&tx_queue_drv, buffer);
        assert(!err);
        num_queued_to_driver++;
        return true;
    }
    return false;
}

static bool tx_provide_fw_client(int client)
{
    if (fw_queue_empty(&fw_active_clients[client])) {
        return false;
    }

    fw_buff_desc_t buffer;
    int err = fw_dequeue(&fw_active_clients[client], &buffer);
    assert(!err);

    assert(buffer.offset % NET_BUFFER_SIZE == 0
           && buffer.offset < NET_BUFFER_SIZE * fw_active_clients[client].capacity);
    assert(buffer.interface < fw_config.num_data_regions);

    uintptr_t buffer_vaddr = buffer.offset + (uintptr_t)fw_config.data_regions[buffer.interface].region.vaddr;
    cache_clean(buffer_vaddr, buffer_vaddr + buffer.len);
    uintptr_t io_addr = buffer.offset + fw_config.data_regions[buffer.interface].io_addr;

    net_buff_desc_t net_buffer = { .io_or_offset = io_addr, .len = buffer.len };
    err = net_enqueue_active(&tx_queue_drv, net_buffer);
    assert(!err);
    num_queued_to_driver++;
    return true;
}

/* Rotates each pass */
static uint32_t net_client_cursor;
static uint32_t fw_client_cursor;

/* One buffer per net client, starting from net_client_cursor. */
static bool tx_provide_net_clients_pass(void)
{
    bool progress = false;
    for (int i = 0; i < config.num_clients && driver_queue_has_space(); i++) {
        int client = (net_client_cursor + i) % config.num_clients;
        if (tx_provide_net_client(client)) {
            progress = true;
        }
    }
    if (config.num_clients > 0) {
        net_client_cursor = (net_client_cursor + 1) % config.num_clients;
    }
    return progress;
}

static bool tx_provide_fw_clients_pass(void)
{
    bool progress = false;
    for (int i = 0; i < fw_config.num_active_clients && driver_queue_has_space(); i++) {
        int client = (fw_client_cursor + i) % fw_config.num_active_clients;
        if (tx_provide_fw_client(client)) {
            progress = true;
        }
    }
    if (fw_config.num_active_clients > 0) {
        fw_client_cursor = (fw_client_cursor + 1) % fw_config.num_active_clients;
    }
    return progress;
}

static void tx_provide(void)
{
    bool enqueued = false;
    bool reprocess = true;

    while (reprocess) {
        reprocess = false;

        bool progress = true;
        while (progress && driver_queue_has_space()) {
            bool net_progress, fw_progress;
            if (net_clients_go_first) {
                net_progress = tx_provide_net_clients_pass();
                fw_progress = tx_provide_fw_clients_pass();
            } else {
                fw_progress = tx_provide_fw_clients_pass();
                net_progress = tx_provide_net_clients_pass();
            }
            net_clients_go_first = !net_clients_go_first;

            progress = net_progress || fw_progress;
            if (progress) {
                enqueued = true;
            }
        }

        for (int client = 0; client < config.num_clients; client++) {
            if (!net_queue_empty_active(&tx_queue_clients[client])) {
                continue;
            }

            net_request_signal_active(&tx_queue_clients[client]);
            if (!net_queue_empty_active(&tx_queue_clients[client])) {
                net_cancel_signal_active(&tx_queue_clients[client]);
                reprocess = true;
            }
        }
    }

    if (enqueued && net_require_signal_active(&tx_queue_drv)) {
        net_cancel_signal_active(&tx_queue_drv);
        microkit_deferred_notify(config.driver.id);
    }
}

static void tx_return(void)
{
    bool reprocess = true;
    bool notify_net_clients[SDDF_NET_MAX_CLIENTS] = { false };
    bool notify_fw_clients[SDDF_NET_MAX_CLIENTS] = { false };
    while (reprocess) {
        while (!net_queue_empty_free(&tx_queue_drv)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_free(&tx_queue_drv, &buffer);
            assert(!err);
            assert(num_queued_to_driver > 0);
            if (num_queued_to_driver > 0) {
                num_queued_to_driver--;
            }

            int client = extract_offset_net_client(&buffer.io_or_offset);
            if (client >= 0) {
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

        if (!net_queue_empty_free(&tx_queue_drv)) {
            net_cancel_signal_free(&tx_queue_drv);
            reprocess = true;
        }
    }

    for (int client = 0; client < config.num_clients; client++) {
        if (notify_net_clients[client] && net_require_signal_free(&tx_queue_clients[client])) {
            net_cancel_signal_free(&tx_queue_clients[client]);
            microkit_notify(config.clients[client].conn.id);
        }
    }

    for (int client = 0; client < fw_config.num_free_clients; client++) {
        if (notify_fw_clients[client]) {
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

    for (int i = 0; i < config.num_clients; i++) {
        net_queue_init(&tx_queue_clients[i], config.clients[i].conn.free_queue.vaddr,
                       config.clients[i].conn.active_queue.vaddr, config.clients[i].conn.num_buffers);
    }

    /* Set up firewall queues */
    for (int i = 0; i < fw_config.num_active_clients; i++) {
        fw_queue_init(&fw_active_clients[i], fw_config.active_clients[i].queue.vaddr, sizeof(fw_buff_desc_t),
                      fw_config.active_clients[i].capacity);
    }

    for (int i = 0; i < fw_config.num_free_clients; i++) {
        fw_queue_init(&fw_free_clients[i], fw_config.free_clients[i].conn.queue.vaddr, sizeof(net_buff_desc_t),
                      fw_config.free_clients[i].conn.capacity);
    }
    tx_provide();
}
