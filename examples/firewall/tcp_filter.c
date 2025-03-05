/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

// NOTE: This is just a generic dummy filter that copies all packets to the router.

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

__attribute__((__section__(".filter_config"))) filter_config_t filter_config;

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

dev_info_t *device_info;

void filter(void)
{
    // Just copy all the buffers to the router.
    bool transmitted = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue) && !net_queue_empty_free(&tx_queue)) {
            net_buff_desc_t rx_buffer;
            int err = net_dequeue_active(&rx_queue, &rx_buffer);
            assert(!err);
            net_buff_desc_t tx_buffer;
            err = net_dequeue_free(&tx_queue, &tx_buffer);
            assert(!err);

            sddf_memcpy((filter_config.data.vaddr + tx_buffer.io_or_offset), (net_config.rx_data.vaddr + rx_buffer.io_or_offset), rx_buffer.len);
            tx_buffer.len = rx_buffer.len;
            rx_buffer.len = 0;
            err = net_enqueue_free(&rx_queue, rx_buffer);
            assert(!err);
            err = net_enqueue_active(&tx_queue, tx_buffer);
            transmitted = true;
        }

        net_request_signal_active(&rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue) && !net_queue_empty_free(&tx_queue) ) {
            net_cancel_signal_active(&rx_queue);
            reprocess = true;
        }
    }


    if (transmitted && net_require_signal_active(&tx_queue)) {
        net_cancel_signal_active(&tx_queue);
        microkit_deferred_notify(filter_config.conn.id);
    }

}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));
    assert(firewall_config_check_magic((void*) &filter_config));

    // Initialise the buffers with the rx virtualiser
    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
        net_config.rx.num_buffers);

    // Initialise all the buffers with the routing component
    net_queue_init(&tx_queue, filter_config.conn.free_queue.vaddr, filter_config.conn.active_queue.vaddr,
        filter_config.conn.num_buffers);
    net_buffers_init(&tx_queue, 0);

    device_info = (dev_info_t *)net_config.dev_info.vaddr;

    return;
}

void notified(microkit_channel ch)
{
    if (ch == net_config.rx.id) {
        filter();
    } else {
        sddf_dprintf("ICMP_FILTER|Received notification on unknown channel: %d!\n", ch);
    }
}