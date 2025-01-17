/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <poll.h>
#include <sys/types.h>

#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>

#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/timer/client.h>

#include <lions/fs/server.h>

#include "nfs.h"
#include "util.h"
#include "tcp.h"
#include "posix.h"

#define TIMEOUT (1 * NS_IN_MS)

__attribute__((__section__(".serial_client_config")))
serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config")))
timer_client_config_t timer_config;

__attribute__((__section__(".net_client_config")))
net_client_config_t net_config;

serial_queue_handle_t serial_tx_queue_handle;

struct nfs_context *nfs;

void notified(microkit_channel ch) {
    if (ch == timer_config.driver_id) {
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
        sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
    } else if (ch == net_config.rx.id) {
        tcp_process_rx();
    } else if (ch == net_config.tx.id || ch == serial_config.tx.id) {
        /* Nothing to do in this case */
    } else {
        dlog("got notification from unknown channel %llu", ch);
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
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.queue.size, serial_config.tx.data.vaddr);

    syscalls_init();
    continuation_pool_init();
    tcp_init_0();
    sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
}
