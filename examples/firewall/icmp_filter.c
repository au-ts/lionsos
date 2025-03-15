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
#include <firewall_queue.h>
#include <config.h>

__attribute__((__section__(".firewall_filter_config"))) firewall_filter_config_t filter_config;

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

firewall_queue_handle_t router_queue;

void filter(void)
{
    bool transmitted = false;
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue, &buffer);
            assert(!err);

            if (FIREWALL_DEBUG_OUTPUT) {
                sddf_printf("MAC[5] = %x | ICMP filter buf no %lu\n", filter_config.mac_addr[5], buffer.io_or_offset/NET_BUFFER_SIZE);
            }

            err = firewall_enqueue(&router_queue, net_firewall_desc(buffer));
            transmitted = true;
        }

        net_request_signal_active(&rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&rx_queue)) {
            net_cancel_signal_active(&rx_queue);
            reprocess = true;
        }
    }

    if (transmitted) {
        microkit_deferred_notify(filter_config.router.ch);
    }
}

void init(void)
{
    assert(net_config_check_magic((void *)&net_config));

    net_queue_init(&rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
        net_config.rx.num_buffers);
    
    firewall_queue_init(&router_queue, filter_config.router.queue.vaddr, filter_config.router.capacity);
}

void notified(microkit_channel ch)
{
    if (ch == net_config.rx.id) {
        filter();
    } else {
        sddf_dprintf("ICMP_FILTER|LOG: Received notification on unknown channel: %d!\n", ch);
    }
}
