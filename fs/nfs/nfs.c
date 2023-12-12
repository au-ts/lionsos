/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <poll.h>
#include <sys/types.h>

#include <sddf/timer/client.h>
#include <sddf/network/shared_ringbuffer.h>

#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>

#include "nfs.h"
#include "util.h"
#include "fd.h"
#include "tcp.h"
#include "posix.h"

#define TIMEOUT (10 * NS_IN_MS)

struct nfs_context *nfs;

void nfs_connect_cb(int err, struct nfs_context *nfs_ctx, void *data, void *private_data) {
    dlogp(err, "failed to connect to nfs server (%d): %s", err, nfs_get_error(nfs));
    dlogp(!err, "connected to nfs server");
}

void nfs_init(void) {
    nfs = nfs_init_context();
    if (nfs == NULL) {
        dlog("failed to init nfs context");
        return;
    }

    int err = nfs_mount_async(nfs, NFS_SERVER, NFS_DIRECTORY, nfs_connect_cb, NULL);
    dlogp(err, "failed to connect to nfs server");
}

void notified(microkit_channel ch) {
    sddf_timer_set_timeout(TIMEOUT);
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
        break;
    }
    case ETHERNET_RX_CHANNEL:
        tcp_process_rx();
        break;
    case CLIENT_CHANNEL: {
        nfs_notified();
        break;
    }
    default:
        dlog("got notification from unknown channel");
        break;
    }

    tcp_maybe_notify();
}

void init(void) {
    syscalls_init();
    continuation_pool_init();
    tcp_init_0();
    sddf_timer_set_timeout(TIMEOUT);
}
