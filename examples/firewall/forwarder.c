/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <microkit.h>
#include <sddf/network/queue.h>
#include <sddf/util/string.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <ethernet_config.h>

#define VIRT_RX_CH 0
#define CLIENT_CH 1

net_queue_handle_t rx_queue_virt;
net_queue_handle_t rx_queue_cli;

net_queue_t *rx_free_virt;
net_queue_t *rx_active_virt;
net_queue_t *rx_free_cli;
net_queue_t *rx_active_cli;

uintptr_t virt_buffer_data_region;

net_queue_handle_t rx_queue_virt;
net_queue_handle_t rx_queue_client;

void rx_return(void)
{
    bool reprocess = true;
    bool notify_client = false;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue_virt)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue_virt, &buffer);
            assert(!err);

            err = net_enqueue_active(&rx_queue_client, buffer);
            assert(!err);
            notify_client = true;
        }
        net_request_signal_active(&rx_queue_virt);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue_virt)) {
            net_cancel_signal_active(&rx_queue_virt);
            reprocess = true;
        }
    }

    if (notify_client && net_require_signal_active(&rx_queue_client)) {
        net_cancel_signal_active(&rx_queue_client);
        microkit_notify(CLIENT_CH);
    }
}

void rx_provide(void)
{
    bool reprocess = true;
    bool notify_virt = false;
    while (reprocess) {
        while (!net_queue_empty_free(&rx_queue_client)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_free(&rx_queue_client, &buffer);
            assert(!err);
            assert(!(buffer.io_or_offset % NET_BUFFER_SIZE) &&
                    (buffer.io_or_offset < NET_BUFFER_SIZE * rx_queue_client.size));

            err = net_enqueue_free(&rx_queue_virt, buffer);
            assert(!err);
            notify_virt = true;
        }

        net_request_signal_free(&rx_queue_client);
        reprocess = false;

        if (!net_queue_empty_free(&rx_queue_client)) {
            net_cancel_signal_free(&rx_queue_client);
            reprocess = true;
        }
    }

    if (notify_virt && net_require_signal_free(&rx_queue_virt)) {
        net_cancel_signal_free(&rx_queue_virt);
        microkit_deferred_notify(VIRT_RX_CH);
        notify_virt = false;
    }
}

void notified(microkit_channel ch)
{
    rx_return();
    rx_provide();
}

void init(void)
{
    /* Set up Rx Virt queues */
    net_queue_init(&rx_queue_client, rx_free_cli, rx_active_cli, NET_RX_QUEUE_SIZE_DRIV); /* TODO: set up queue size properly. */
    /* Set up Tx Virt queues (for the other driver) */
    net_queue_init(&rx_queue_virt, rx_free_virt, rx_active_virt, NET_RX_QUEUE_SIZE_DRIV); /* TODO: set up queue size properly */
}
