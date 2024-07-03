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

#include "nfs.h"
#include "util.h"
#include "fd.h"
#include "tcp.h"
#include "posix.h"

#ifndef ETHERNET_RX_CHANNEL
#error "Expected ETHERNET_RX_CHANNEL to be defined"
#endif

#ifndef CLIENT_CHANNEL
#error "Expected CLIENT_CHANNEL to be defined"
#endif

#define TIMEOUT (1 * NS_IN_MS)

struct nfs_context *nfs;
static bool nfs_connected;

void nfs_connect_cb(int err, struct nfs_context *nfs_ctx, void *data, void *private_data) {
    if (!err) {
        nfs_connected = true;
        nfs_notified();
        dlog("connected to nfs server");
    } else {
        dlog("failed to connect to nfs server (%d): %s", err, data);
    }
}

void nfs_init(void) {
    nfs = nfs_init_context();
    if (nfs == NULL) {
        dlog("failed to init nfs context");
        return;
    }

    /*
     * We want to have the NFS client attempt to re-connect indefinitely when our connection
     * with the server is closed or dropped.
     */
    nfs_set_autoreconnect(nfs, -1);

    int err = nfs_mount_async(nfs, NFS_SERVER, NFS_DIRECTORY, nfs_connect_cb, NULL);
    dlogp(err, "failed to connect to nfs server");
}

void notified(microkit_channel ch) {
    switch (ch) {
    case TIMER_CHANNEL: {
        tcp_process_rx();
        tcp_update();
        if (tcp_ready() && nfs == NULL) {
            dlog("network ready, initing nfs");
            nfs_init();
        }
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
    case CLIENT_CHANNEL:
        if (nfs_connected) {
            nfs_notified();
        }
        break;
    default:
        dlog("got notification from unknown channel %llu", ch);
        break;
    }

    tcp_maybe_notify();
}

void init(void)
{
    syscalls_init();
    continuation_pool_init();
    tcp_init_0();
    sddf_timer_set_timeout(TIMER_CHANNEL, TIMEOUT);
}
