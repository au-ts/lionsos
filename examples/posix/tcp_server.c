/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>

#include <microkit.h>
#include <libmicrokitco.h>

#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/i2c/config.h>
#include <sddf/i2c/queue.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/timer/protocol.h>
#include <sddf/network/config.h>
#include <sddf/network/queue.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/firewall/arp.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/queue.h>
#include <lions/util.h>

#include <lions/fs/helpers.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>

#define TIMEOUT (1 * NS_IN_MS)

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_tx_queue_handle;
serial_queue_handle_t serial_rx_queue_handle;

net_queue_handle_t net_rx_handle;
net_queue_handle_t net_tx_handle;

extern libc_socket_config_t socket_config;

bool fs_enabled;
bool serial_rx_enabled;
bool net_enabled;

#define LIBC_COTHREAD_STACK_SIZE 0x10000

static char libc_cothread_stack[LIBC_COTHREAD_STACK_SIZE];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

static void netif_status_callback(char *ip_addr) {
    printf("TCP_SERVER|INFO: DHCP request finished, IP address is: %s\n", ip_addr);
}

void notified(microkit_channel ch) {
    if (ch == timer_config.driver_id) {
        if (net_enabled) {
            sddf_lwip_process_rx();
            sddf_lwip_process_timeout();

            sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
        }
    } else if (ch == net_config.rx.id) {
        if (net_enabled) {
            sddf_lwip_process_rx();
        }
    }
    microkit_cothread_recv_ntfn(ch);

    if (net_enabled) {
        sddf_lwip_maybe_notify();
    }
}

static int sockaddr_to_string(struct sockaddr *addr, char *str, size_t len) {
    uint16_t port;
    char ip_string[64];
    void *addr_buf;
    int ret;

    switch (addr->sa_family) {
    case AF_INET: {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
        port = addr_in->sin_port;
        addr_buf = &addr_in->sin_addr;
        break;
    }
    case AF_INET6: {
        struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
        port = addr_in6->sin6_port;
        addr_buf = &addr_in6->sin6_addr;
        break;
    }
    default:
        return -1;
    }

    inet_ntop(addr->sa_family, addr_buf, ip_string, sizeof(ip_string) / sizeof(ip_string[0]));

    ret = snprintf(str, len, "%s:%d", ip_string, ntohs(port));

    return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static void init_sockaddr_inet(struct sockaddr_in *addr) {
    addr->sin_family = AF_INET;
    addr->sin_port = htons(1234);
    inet_pton(AF_INET, "10.0.2.15", &(addr->sin_addr.s_addr));
}

void cont() {
    libc_init(&socket_config);

    if (net_enabled) {
        printf("TCP_SERVER|INFO: init\n");

        net_queue_init(&net_rx_handle, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                       net_config.rx.num_buffers);
        net_queue_init(&net_tx_handle, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                       net_config.tx.num_buffers);
        net_buffers_init(&net_tx_handle, 0);

        sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle, net_tx_handle, NULL, printf,
                       netif_status_callback, NULL, NULL, NULL);

        sddf_lwip_maybe_notify();

        int socket_fd = -1, addrlen = 0, af;
        struct sockaddr_storage addr = { 0 };
        char ip_string[64];

        af = AF_INET;
        addrlen = sizeof(struct sockaddr_in);
        init_sockaddr_inet((struct sockaddr_in *)&addr);

        printf("TCP_SERVER|INFO: Create socket\n");
        socket_fd = socket(af, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            fprintf(stderr, "TCP_SERVER|ERROR: Create socket failed");
            goto fail;
        }

        printf("TCP_SERVER|INFO: Bind socket\n");
        if (bind(socket_fd, (struct sockaddr *)&addr, addrlen) < 0) {
            fprintf(stderr, "TCP_SERVER|ERROR: Bind failed");
            goto fail;
        }

        printf("TCP_SERVER|INFO: Listening on socket\n");
        if (listen(socket_fd, 10) < 0) {
            fprintf(stderr, "TCP_SERVER|ERROR: Listen failed");
            goto fail;
        }

        printf("TCP_SERVER|INFO: Wait for client to connect ..\n");
        addrlen = sizeof(struct sockaddr);

        int new_socket = accept(socket_fd, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            fprintf(stderr, "TCP_SERVER|ERROR: Accept failed");
            goto fail;
        }

        if (sockaddr_to_string((struct sockaddr *)&addr, ip_string, sizeof(ip_string) / sizeof(ip_string[0])) != 0) {
            printf("TCP_SERVER|ERROR: failed to parse client address\n");
            goto fail;
        }

        printf("TCP_SERVER|INFO: Client connected (%s), fd %d\n", ip_string, new_socket);

        const char *message = "Hi from the Server\n";

        if (send(new_socket, message, strlen(message), 0) < 0) {
            fprintf(stderr, "TCP_SERVER|ERROR: Send failed");
        }

        printf("TCP_SERVER|INFO: Shutting down connection fd %d ..\n", new_socket);
        shutdown(new_socket, SHUT_RDWR);

        printf("TCP_SERVER|INFO: Shutting down ..\n");
        shutdown(socket_fd, SHUT_RDWR);
        printf("TCP_SERVER|INFO: BYE \n");
        return;

    fail:
        printf("TCP_SERVER|INFO: Shutting down ..\n");
        if (socket_fd >= 0)
            close(socket_fd);
        return;
    }
}

void init() {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    fs_enabled = fs_config_check_magic(&fs_config);
    net_enabled = net_config_check_magic(&net_config);
    serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);

    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    if (fs_enabled) {
        fs_set_blocking_wait(blocking_wait);
        fs_command_queue = fs_config.server.command_queue.vaddr;
        fs_completion_queue = fs_config.server.completion_queue.vaddr;
        fs_share = fs_config.server.share.vaddr;
    }

    stack_ptrs_arg_array_t costacks = { (uintptr_t)libc_cothread_stack };
    microkit_cothread_init(&co_controller_mem, LIBC_COTHREAD_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(cont, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        fprintf(stderr, "TCP_SERVER|ERROR: Cannot initialise cothread\n");
        assert(false);
    };

    sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);

    microkit_cothread_yield();
}
