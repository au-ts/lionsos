/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <poll.h>
#include <sys/types.h>

#include <sddf/timer/client.h>
#include <sddf/network/queue.h>

#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>

#include <sddf/serial/queue.h>
#include <serial_config.h>

#include <lions/fs/server.h>

#include "nfs.h"
#include "util.h"
#include "tcp.h"
#include "posix.h"

#ifndef ETHERNET_RX_CHANNEL
#error "Expected ETHERNET_RX_CHANNEL to be defined"
#endif

#ifndef CLIENT_CHANNEL
#error "Expected CLIENT_CHANNEL to be defined"
#endif

#define TIMEOUT (1 * NS_IN_MS)

char *serial_tx_data;
serial_queue_t *serial_tx_queue;
serial_queue_handle_t serial_tx_queue_handle;

struct nfs_context *nfs;

void notified(microkit_channel ch) {
    switch (ch) {
    case TIMER_CHANNEL: {
        tcp_process_rx();
        tcp_update();
        if (nfs != NULL) {
            int nfs_fd = nfs_get_fd(nfs);
            int socket_index = socket_index_of_fd(nfs_fd);
            int revents = nfs_which_events(nfs);
            int sevents = 0;
            if (tcp_socket_hup(socket_index)) {
                sevents |= POLLHUP;
            }
            if (tcp_socket_err(socket_index)) {
                sevents |= POLLERR;
            }
            if (revents & POLLOUT && tcp_socket_writable(socket_index)) {
                sevents |= POLLOUT;
            }
            if (revents & POLLIN && tcp_socket_readable(socket_index)) {
                sevents |= POLLIN;
            }
            if (sevents) {
                int err = nfs_service(nfs, sevents);
                dlogp(err, "nfs_service error");
            }
        }
        sddf_timer_set_timeout(TIMER_CHANNEL, TIMEOUT);
        break;
    }
    case ETHERNET_RX_CHANNEL:
        tcp_process_rx();
        break;
    case ETHERNET_TX_CHANNEL:
        /* Nothing to do in this case */
        break;
    case SERIAL_TX_CH:
        /* Nothing to do in this case */
        break;
    case CLIENT_CHANNEL:
        /* Handled outside of switch */
        break;
    default:
        dlog("got notification from unknown channel %llu", ch);
        break;
    }

    // If we leave any commands in the queue, we can't rely on another client
    // notification to cause us to try to reprocess those commands, hence we
    // try to process commands unconditionally on any notification
    if (tcp_ready()) {
        process_commands();
    }
    tcp_maybe_notify();
}

void init(void)
{
    serial_cli_queue_init_sys(microkit_name, NULL, NULL, NULL, &serial_tx_queue_handle, serial_tx_queue, serial_tx_data);

    syscalls_init();
    continuation_pool_init();
    tcp_init_0();
    sddf_timer_set_timeout(TIMER_CHANNEL, TIMEOUT);
}
