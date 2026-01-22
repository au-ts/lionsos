/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>

#include <microkit.h>
#include <libmicrokitco.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/timer/protocol.h>
#include <sddf/network/config.h>
#include <sddf/network/queue.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

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

bool net_enabled;
bool serial_rx_enabled;

#define LIBC_COTHREAD_STACK_SIZE 0x10000
static char libc_cothread_stack[LIBC_COTHREAD_STACK_SIZE];
static co_control_t co_controller_mem;

static bool dhcp_ready = false;

static void netif_status_callback(char *ip_addr) {
    printf("POSIX_TEST|server|INFO|DHCP: %s\n", ip_addr);
    dhcp_ready = true;
}

#define TEST_COMPONENT "server"
#include "test_helpers.h"

#define TEST_PORT 5555
#define TEST_PORT_BLOCKING 5560
#define TEST_PORT_NONBLOCK 5561

#define SERVER_IP "10.0.2.15"
#define HOST_IP "10.0.2.2"
#define CLIENT_NTFN_CH 0

static bool test_listen() {
    int sock = -1;
    bool result = false;
    struct sockaddr_in addr;

    printf("Listen on bound socket succeeds...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    EXPECT_OK(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    EXPECT_OK(listen(sock, 5) == 0);
    printf("OK\n");

    printf("Listen on closed socket fails with EBADF...");
    close(sock);
    EXPECT_ERR(listen(sock, 5), EBADF);
    sock = -1;
    printf("OK\n");

    printf("Listen on file FD fails with ENOTSOCK...");
    EXPECT_ERR(listen(STDOUT_FD, 5), ENOTSOCK);
    printf("OK\n");

    printf("listen same port twice fails with EADDRINUSE...");
    int sock2 = -1;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT + 2);
    addr.sin_addr.s_addr = INADDR_ANY;
    EXPECT_OK(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    EXPECT_OK(listen(sock, 5) == 0);

    sock2 = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock2 >= 0);
    EXPECT_ERR(bind(sock2, (struct sockaddr *)&addr, sizeof(addr)), EADDRINUSE);
    close(sock);
    close(sock2);
    sock = -1;
    sock2 = -1;
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_accept() {
    int sock = -1;
    bool result = false;
    struct sockaddr_in addr;

    printf("Accept on invalid FD fails with EBADF...");
    EXPECT_ERR(accept(-1, NULL, NULL), EBADF);
    printf("OK\n");

    printf("Accept on file FD fails with ENOTSOCK...");
    EXPECT_ERR(accept(STDOUT_FD, NULL, NULL), ENOTSOCK);
    printf("OK\n");

    printf("Accept nonblocking, no clients fails with EAGAIN...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT + 1);
    addr.sin_addr.s_addr = INADDR_ANY;
    EXPECT_OK(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    EXPECT_OK(listen(sock, 5) == 0);
    EXPECT_OK(fcntl(sock, F_SETFL, O_NONBLOCK) == 0);
    EXPECT_ERR(accept(sock, NULL, NULL), EAGAIN);
    printf("OK\n");

    printf("accept on non-listening socket fails with EINVAL...");
    int sock3 = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock3 >= 0);
    addr.sin_port = htons(TEST_PORT + 3);
    EXPECT_OK(bind(sock3, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    EXPECT_ERR(accept(sock3, NULL, NULL), EINVAL);
    close(sock3);
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_blocking_echo() {
    int listen_fd = -1;
    int client_fd = -1;
    bool result = false;
    struct sockaddr_in addr, peer_addr, local_addr;
    socklen_t addr_len;
    char buf[64];
    const char *expected = "PING";

    printf("POSIX_TEST|server|INFO|Setting up server...\n");

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(listen_fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_BLOCKING);
    addr.sin_addr.s_addr = INADDR_ANY;
    EXPECT_OK(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    addr_len = sizeof(local_addr);

    EXPECT_OK(getsockname(listen_fd, (struct sockaddr *)&local_addr, &addr_len) == 0);
    EXPECT_OK(ntohs(local_addr.sin_port) == TEST_PORT_BLOCKING);
    EXPECT_OK(listen(listen_fd, 5) == 0);
    printf("POSIX_TEST|server|INFO|Listening on %d, notifying client...\n", ntohs(local_addr.sin_port));

    /* Notify client that server is ready */
    microkit_notify(CLIENT_NTFN_CH);

    printf("Accept connecting client should succeed...");
    addr_len = sizeof(peer_addr);
    client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
    EXPECT_OK(client_fd >= 0);
    EXPECT_OK(strcmp(inet_ntoa(peer_addr.sin_addr), HOST_IP) == 0);
    printf("OK\n");

    printf("getpeername on accepted socket should match...");
    addr_len = sizeof(peer_addr);
    EXPECT_OK(getpeername(client_fd, (struct sockaddr *)&peer_addr, &addr_len) == 0);
    EXPECT_OK(strcmp(inet_ntoa(peer_addr.sin_addr), HOST_IP) == 0);
    printf("OK\n");

    printf("getsockname on accepted socket should match...");
    addr_len = sizeof(local_addr);
    EXPECT_OK(getsockname(client_fd, (struct sockaddr *)&local_addr, &addr_len) == 0);
    EXPECT_OK(strcmp(inet_ntoa(local_addr.sin_addr), SERVER_IP) == 0);
    printf("OK\n");

    ssize_t received = recv(client_fd, buf, sizeof(buf) - 1, 0);
    EXPECT_OK(received > 0);
    buf[received] = '\0';
    printf("POSIX_TEST|server|INFO|Blocking: Received: %s\n", buf);
    EXPECT_OK(strcmp(buf, expected) == 0);

    ssize_t sent = send(client_fd, buf, received, 0);
    EXPECT_OK(sent == received);
    printf("POSIX_TEST|server|INFO|Blocking: Echoed: %s\n", buf);

    result = true;

cleanup:
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    return result;
}

static bool test_nonblocking_echo() {
    int listen_fd = -1;
    int client_fd = -1;
    bool result = false;
    struct sockaddr_in addr, peer_addr;
    socklen_t addr_len;
    char buf[64];
    const char *expected = "PING_NB";
    int retry;

    printf("POSIX_TEST|server|INFO|Non-blocking: Setting up server...\n");

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(listen_fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_NONBLOCK);
    addr.sin_addr.s_addr = INADDR_ANY;
    EXPECT_OK(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    EXPECT_OK(listen(listen_fd, 5) == 0);

    EXPECT_OK(fcntl(listen_fd, F_SETFL, O_NONBLOCK) == 0);
    printf("POSIX_TEST|server|INFO|Non-blocking: Listening, notifying client...\n");

    /* Notify client that server is ready */
    microkit_notify(CLIENT_NTFN_CH);

    printf("POSIX_TEST|server|INFO|Non-blocking: Waiting for client to connect...\n");
    struct pollfd pfd = { .fd = listen_fd, .events = POLLIN };
    for (retry = 0; retry < 1000; retry++) {
        int ret = poll(&pfd, 1, 0);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            addr_len = sizeof(peer_addr);
            client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
            if (client_fd >= 0) {
                break;
            }
        }
        microkit_cothread_yield();
    }
    EXPECT_OK(client_fd >= 0);
    printf("POSIX_TEST|server|INFO|Non-blocking: Accepted after %d retries\n", retry);

    EXPECT_OK(fcntl(client_fd, F_SETFL, O_NONBLOCK) == 0);

    pfd.fd = client_fd;
    pfd.events = POLLIN;
    for (retry = 0; retry < 1000; retry++) {
        int ret = poll(&pfd, 1, 0);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t received = recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (received > 0) {
                buf[received] = '\0';
                printf("POSIX_TEST|server|INFO|Non-blocking: Received: %s\n", buf);
                EXPECT_OK(strcmp(buf, expected) == 0);

                ssize_t sent = send(client_fd, buf, received, 0);
                EXPECT_OK(sent == received);
                printf("POSIX_TEST|server|INFO|Non-blocking: Echoed %s\n", buf);
                break;
            }
        }
        microkit_cothread_yield();
    }
    EXPECT_OK(retry < 1000);

    /* Wait for client to indicate they've finished before we clean up */
    microkit_cothread_wait_on_channel(CLIENT_NTFN_CH);

    result = true;

cleanup:
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    return result;
}

static bool test_connect_refused_sync() {
    /* Wait for client to complete connect-refused test */
    microkit_cothread_wait_on_channel(CLIENT_NTFN_CH);
    printf("POSIX_TEST|server|INFO|Client completed connect-refused test\n");
    return true;
}

void run_tests(void) {
    printf("POSIX_TEST|server|START\n");

    if (!test_listen()) {
        return;
    }
    if (!test_accept()) {
        return;
    }
    printf("POSIX_TEST|server|INFO|Unit tests passed\n");

    if (!test_connect_refused_sync()) {
        return;
    }
    if (!test_blocking_echo()) {
        return;
    }
    if (!test_nonblocking_echo()) {
        return;
    }

    printf("POSIX_TEST|server|PASS\n");
}

void cont(void) {
    libc_init(&socket_config);

    if (net_enabled) {
        net_queue_init(&net_rx_handle, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                       net_config.rx.num_buffers);
        net_queue_init(&net_tx_handle, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                       net_config.tx.num_buffers);
        net_buffers_init(&net_tx_handle, 0);

        sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle, net_tx_handle, NULL, printf,
                       netif_status_callback, NULL, NULL, NULL);

        sddf_lwip_maybe_notify();

        /* Wait for DHCP lease before running tests */
        printf("POSIX_TEST|server|INFO|Waiting for DHCP...\n");
        while (!dhcp_ready) {
            microkit_cothread_yield();
        }
        printf("POSIX_TEST|server|INFO|DHCP ready, running tests\n");

        run_tests();
    } else {
        printf("POSIX_TEST|server|SKIP|Network not enabled\n");
    }
}

void notified(microkit_channel ch) {
    if (ch == timer_config.driver_id && net_enabled) {
        sddf_lwip_process_rx();
        sddf_lwip_process_timeout();
        sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
    } else if (ch == net_config.rx.id && net_enabled) {
        sddf_lwip_process_rx();
    }
    microkit_cothread_recv_ntfn(ch);

    if (net_enabled) {
        sddf_lwip_maybe_notify();
    }

    microkit_cothread_yield();
}

void init(void) {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    net_enabled = net_config_check_magic(&net_config);

    serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);
    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    stack_ptrs_arg_array_t costacks = { (uintptr_t)libc_cothread_stack };
    microkit_cothread_init(&co_controller_mem, LIBC_COTHREAD_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(cont, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("POSIX_TEST|server|ERROR|Cannot initialise cothread\n");
        assert(false);
    }

    sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
    microkit_cothread_yield();
}
