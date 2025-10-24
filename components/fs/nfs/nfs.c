/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>

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

#include <sddf/network/lib_sddf_lwip.h>

#include <lions/fs/server.h>
#include <lions/fs/config.h>

#include <lions/util.h>
#include <lions/sock/tcp.h>

#include "nfs.h"
#include "config.h"

#define TIMEOUT (1 * NS_IN_MS)

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".fs_server_config"))) fs_server_config_t fs_config;
__attribute__((__section__(".nfs_config"))) nfs_config_t nfs_config;
__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;

serial_queue_handle_t serial_tx_queue_handle;

struct fs_queue *fs_command_queue;
struct fs_queue *fs_completion_queue;
char *fs_share;

struct nfs_context *nfs;

net_queue_handle_t net_rx_handle;
net_queue_handle_t net_tx_handle;

extern libc_socket_config_t socket_config;

static bool network_ready = false;

static void netif_status_callback(char *ip_addr) {
    printf("%s: %s:%d:%s: DHCP request finished, IP address for %s is: %s\r\n", microkit_name, __FILE__, __LINE__,
           __func__, microkit_name, ip_addr);

    network_ready = true;
}

void notified(microkit_channel ch) {
    if (ch == timer_config.driver_id) {
        sddf_lwip_process_rx();
        sddf_lwip_process_timeout();

        if (nfs != NULL) {
            int nfs_fd = nfs_get_fd(nfs);
            int socket_index = socket_index_of_fd(nfs_fd);
            int revents = nfs_which_events(nfs);
            int sevents = 0;
            if (socket_config.tcp_socket_hup(socket_index)) {
                sevents |= POLLHUP;
            }
            if (socket_config.tcp_socket_err(socket_index)) {
                sevents |= POLLERR;
            }
            if (revents & POLLOUT && socket_config.tcp_socket_writable(socket_index)) {
                sevents |= POLLOUT;
            }
            if (revents & POLLIN && socket_config.tcp_socket_readable(socket_index)) {
                sevents |= POLLIN;
            }
            if (sevents) {
                int err = nfs_service(nfs, sevents);
                dlogp(err, "nfs_service error");
            }
        }
        sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
    } else if (ch == net_config.rx.id) {
        sddf_lwip_process_rx();
    } else if (ch == net_config.tx.id || ch == serial_config.tx.id) {
        /* Nothing to do in this case */
    } else if (ch == fs_config.client.id) {
        /* Handled outside of this if statement */
    } else {
        dlog("got notification from unknown channel %llu", ch);
    }

    // If we leave any commands in the queue, we can't rely on another client
    // notification to cause us to try to reprocess those commands, hence we
    // try to process commands unconditionally on any notification
    if (network_ready) {
        process_commands();
    }
    sddf_lwip_maybe_notify();
}

void init(void)
{
    assert(fs_config_check_magic(&fs_config));
    assert(nfs_config_check_magic(&nfs_config));

    fs_command_queue = fs_config.client.command_queue.vaddr;
    fs_completion_queue = fs_config.client.completion_queue.vaddr;
    fs_share = fs_config.client.share.vaddr;

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

    libc_init(&socket_config);
    continuation_pool_init();

    net_queue_init(&net_rx_handle, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);
    net_queue_init(&net_tx_handle, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                   net_config.tx.num_buffers);
    net_buffers_init(&net_tx_handle, 0);

    sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle, net_tx_handle, NULL, printf,
                   netif_status_callback, NULL, NULL, NULL);

    sddf_lwip_maybe_notify();

    sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
}
