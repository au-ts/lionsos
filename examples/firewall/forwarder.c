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

#define VIRT_RX_CH  0
#define CLIENT_CH   1
#define BENCH_START 2
#define BENCH_STOP  3   

net_queue_handle_t rx_queue_virt;
net_queue_handle_t rx_queue_cli;

net_queue_t *rx_free_virt;
net_queue_t *rx_active_virt;
net_queue_t *rx_free_cli;
net_queue_t *rx_active_cli;

uintptr_t virt_buffer_data_region;

net_queue_handle_t rx_queue_virt;
net_queue_handle_t rx_queue_client;

static void start_stop_benchmarking(net_buff_desc_t* buffer) {
    uint8_t ethernet_header_size = 14;
    if (buffer->len < ethernet_header_size)
        return;

    /* check if ip packet */
    uint8_t *ethernet_frame = (uint8_t*) (virt_buffer_data_region + buffer->io_or_offset);
    uint8_t data_first_byte = ethernet_frame[ethernet_header_size];
    /* packet contains a ip packet if the first 4 bits are equal to ipv4 protocol version */
    if (data_first_byte >> 4 == 0b0100) {
        /* check if tcp packet by looking at the protocol field of ip frame, 6 for tcp */
        uint8_t ip_protocol_field = ethernet_frame[ethernet_header_size + 9];
        if (ip_protocol_field == 6){
            /* IHL field specifies the number of 32bit words, so multiply by 4 to get length in bytes */
            uint16_t ip_header_length = (data_first_byte & 0b00001111) * 4;
            /* 2-3 bytes are the 16b length field in IP packet */    
            uint16_t ip_total_length = ethernet_frame[ethernet_header_size + 2];
            ip_total_length = (ip_total_length << 8) + ethernet_frame[ethernet_header_size + 3];

            /* 
                tcp header size is given by the data offset field in tcp header (first 4 bits of 12th index in header)
                gives length in number of 32-bit words, so, multiply by 4.
            */
            uint8_t tcp_data_offset = (ethernet_frame[ethernet_header_size + ip_header_length + 12] >> 4) * 4;
            uint16_t tcp_data_start = ethernet_header_size + ip_header_length + tcp_data_offset;
            uint16_t tcp_length = ip_total_length - ip_header_length - tcp_data_offset;
            // Too precious to throw away...
            // uint16_t tcp_data_end = tcp_data_start + tcp_length;
            // for (uint16_t i = tcp_data_start; i < tcp_data_end; i++) {
            //     sddf_dprintf("%c", ethernet_frame[i]);
            // }
            // sddf_dprintf("\n");

            if (tcp_length >= 4) {
                uint8_t first_byte = ethernet_frame[tcp_data_start];
                uint8_t second_byte = ethernet_frame[tcp_data_start + 1];
                uint8_t third_byte = ethernet_frame[tcp_data_start + 2];
                uint8_t fourth_byte = ethernet_frame[tcp_data_start + 3];
                if (first_byte == 'S' && second_byte == 'T' && third_byte == 'O' && fourth_byte == 'P') {
                    sddf_dprintf("found STOP\n");
                    microkit_notify(BENCH_STOP);
                }
            }

            if (tcp_length >= 5) {
                uint8_t first_byte = ethernet_frame[tcp_data_start];
                uint8_t second_byte = ethernet_frame[tcp_data_start + 1];
                uint8_t third_byte = ethernet_frame[tcp_data_start + 2];
                uint8_t fourth_byte = ethernet_frame[tcp_data_start + 3];
                uint8_t fifth_byte = ethernet_frame[tcp_data_start + 4];
                if (first_byte == 'S' && second_byte == 'T' && third_byte == 'A' && fourth_byte == 'R' && fifth_byte == 'T') {
                    sddf_dprintf("found START\n");
                    microkit_notify(BENCH_START);
                }
            }
        }
    }
}

void tx_provide(void)
{
    bool reprocess = true;
    bool notify_client = false;
    while (reprocess) {
        while (!net_queue_empty_active(&rx_queue_virt)) {
            net_buff_desc_t buffer;
            int err = net_dequeue_active(&rx_queue_virt, &buffer);
            assert(!err);

            if (!sddf_strcmp(NET_COPY0_NAME, "eth1_forwarder"))
                start_stop_benchmarking(&buffer);

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

void tx_return(void)
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
    tx_provide();
    tx_return();
}

void init(void)
{
    /* Set up Rx Virt queues */
    net_queue_init(&rx_queue_client, rx_free_cli, rx_active_cli, NET_RX_QUEUE_SIZE_DRIV); /* TODO: set up queue size properly. */
    /* Set up Tx Virt queues (for the other driver) */
    net_queue_init(&rx_queue_virt, rx_free_virt, rx_active_virt, NET_RX_QUEUE_SIZE_DRIV); /* TODO: set up queue size properly */
}
