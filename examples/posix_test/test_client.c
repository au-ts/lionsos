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
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>

#define TIMEOUT (1 * NS_IN_MS)

#define TEST_COMPONENT "client"
#include "test_helpers.h"

#define TEST_PORT_BIND 5556
#define TEST_PORT_CONNECT 5557
#define TEST_PORT_SOCKNAME 5558
#define TEST_PORT_REFUSED 5559
#define TEST_PORT_BLOCKING 5560
#define TEST_PORT_NONBLOCK 5561

#define CLIENT_IP "10.0.2.16" // client IP in QEMU
#define HOST_IP "10.0.2.2"  // QEMU host gateway for port forwarding

#define SERVER_NTFN_CH 0

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
    printf("POSIX_TEST|client|INFO|DHCP: %s\n", ip_addr);
    dhcp_ready = true;
}

static bool test_socket() {
    int sock = -1;
    int fds[MAX_FDS];
    int allocated = 0;
    bool result = false;

    printf("Create AF_INET/SOCK_STREAM socket should succeed...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    close(sock);
    sock = -1;
    printf("OK\n");

    printf("Create AF_INET6 socket should fail with EAFNOSUPPORT...");
    EXPECT_ERR(socket(AF_INET6, SOCK_STREAM, 0), EAFNOSUPPORT);
    printf("OK\n");

    printf("Create SOCK_DGRAM socket should fail with ESOCKTNOSUPPORT...");
    EXPECT_ERR(socket(AF_INET, SOCK_DGRAM, 0), ESOCKTNOSUPPORT);
    printf("OK\n");

    printf("Exhausting MAX_FDS should fail with EMFILE...");
    for (allocated = 0; allocated < MAX_FDS; allocated++) {
        fds[allocated] = socket(AF_INET, SOCK_STREAM, 0);
        if (fds[allocated] < 0) {
            EXPECT_OK(errno == EMFILE);
            break;
        }
    }
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    for (int i = 0; i < allocated; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    return result;
}

static bool test_bind() {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_BIND);
    addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

    int sock = -1;
    int sock2 = -1;
    bool result = false;

    printf("Binding an invalid FD should fail with EBADF...");
    EXPECT_ERR(bind(STDERR_FD + 1, (struct sockaddr *)&addr, sizeof(addr)), EBADF);
    printf("OK\n");

    printf("Binding to non-socket FD should fail with ENOTSOCK...");
    EXPECT_ERR(bind(STDOUT_FD, (struct sockaddr *)&addr, sizeof(addr)), ENOTSOCK);
    printf("OK\n");

    printf("Binding to NULL address should fail with EFAULT...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_ERR(bind(sock, NULL, sizeof(addr)), EFAULT);
    close(sock);
    sock = -1;
    printf("OK\n");

    printf("Binding to a valid address should succeed...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_OK(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    printf("OK\n");

    printf("Binding already bound socket should fail with EINVAL...");
    EXPECT_ERR(bind(sock, (struct sockaddr *)&addr, sizeof(addr)), EINVAL);
    printf("OK\n");

    printf("Binding unavailable address should fail with EADDRNOTAVAIL...");
    sock2 = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock2 >= 0);
    addr.sin_addr.s_addr = inet_addr("0.0.0.1...");
    EXPECT_ERR(bind(sock2, (struct sockaddr *)&addr, sizeof(addr)), EADDRNOTAVAIL);
    addr.sin_addr.s_addr = inet_addr(CLIENT_IP);
    close(sock2);
    sock2 = -1;
    printf("OK\n");
    /* sock stays open for next test */

    printf("Binding twice to the same address should fail with EADDRINUSE...");
    sock2 = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock2 >= 0);
    EXPECT_ERR(bind(sock2, (struct sockaddr *)&addr, sizeof(addr)), EADDRINUSE);
    close(sock2);
    sock2 = -1;
    printf("OK\n");

    printf("Binding to AF_INET6 addr should fail with EAFNOSUPPORT...");
    struct sockaddr_in6 addr6;
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    sock2 = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock2 >= 0);
    EXPECT_ERR(bind(sock2, (struct sockaddr *)&addr6, sizeof(addr6)), EAFNOSUPPORT);
    close(sock2);
    sock2 = -1;
    printf("OK\n");

    printf("Binding with addrlen too small should fail with EINVAL...");
    sock2 = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock2 >= 0);
    EXPECT_ERR(bind(sock2, (struct sockaddr *)&addr, sizeof(addr) - 1), EINVAL);
    close(sock2);
    sock2 = -1;
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    if (sock2 >= 0) {
        close(sock2);
    }
    return result;
}

static bool test_connect() {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_CONNECT);
    addr.sin_addr.s_addr = inet_addr(HOST_IP);

    int sock = -1;
    bool result = false;

    printf("Connect bad FD fails with EBADF...");
    EXPECT_ERR(connect(-1, (struct sockaddr *)&addr, sizeof(addr)), EBADF);
    printf("OK\n");

    printf("Connect file FD fails with ENOTSOCK...");
    EXPECT_ERR(connect(STDOUT_FD, (struct sockaddr *)&addr, sizeof(addr)), ENOTSOCK);
    printf("OK\n");

    printf("Connect NULL addr fails with EFAULT...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_ERR(connect(sock, NULL, sizeof(addr)), EFAULT);
    close(sock);
    sock = -1;
    printf("OK\n");

    printf("Connect AF_INET6 addr fails with EAFNOSUPPORT...");
    struct sockaddr_in6 addr6;
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_ERR(connect(sock, (struct sockaddr *)&addr6, sizeof(addr6)), EAFNOSUPPORT);
    close(sock);
    sock = -1;
    printf("OK\n");

    printf("Connect addrlen too small fails with EINVAL...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_ERR(connect(sock, (struct sockaddr *)&addr, sizeof(addr) - 1), EINVAL);
    close(sock);
    sock = -1;
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_sockopt() {
    int sock = -1;
    bool result = false;
    int val = 1;
    socklen_t len = sizeof(val);

    printf("setsockopt SO_LINGER succeeds (ignored no-op)...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    struct linger ling = { 1, 0 };
    EXPECT_OK(setsockopt(sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)) == 0);
    printf("OK\n");

    printf("setsockopt TCP_NODELAY fails with ENOPROTOOPT...");
    EXPECT_ERR(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)), ENOPROTOOPT);
    printf("OK\n");

    printf("setsockopt with NULL optval should fail with EINVAL...");
    EXPECT_ERR(setsockopt(sock, SOL_SOCKET, SO_LINGER, NULL, sizeof(ling)), EINVAL);
    printf("OK\n");

    printf("setsockopt with bad FD should fail with EBADF...");
    EXPECT_ERR(setsockopt(-1, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)), EBADF);
    printf("OK\n");

    printf("getsockopt SO_ERROR succeeds...");
    int err = 0;
    len = sizeof(err);
    EXPECT_OK(getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) == 0);
    EXPECT_OK(err == 0);
    printf("OK\n");

    printf("getsockopt with NULL optval should fail with EFAULT...");
    EXPECT_ERR(getsockopt(sock, SOL_SOCKET, SO_ERROR, NULL, &len), EFAULT);
    printf("OK\n");

    printf("getsockopt with NULL optlen should fail with EFAULT...");
    EXPECT_ERR(getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, NULL), EFAULT);
    printf("OK\n");

    printf("getsockopt on non-socket fails with ENOTSOCK...");
    EXPECT_ERR(getsockopt(STDOUT_FD, SOL_SOCKET, SO_ERROR, &err, &len), ENOTSOCK);
    printf("OK\n");

    printf("getsockopt with bad FD should fail with EBADF...");
    EXPECT_ERR(getsockopt(-1, SOL_SOCKET, SO_ERROR, &err, &len), EBADF);
    printf("OK\n");

    printf("getsockopt unsupported fails with ENOPROTOOPT...");
    EXPECT_ERR(getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &val, &len), ENOPROTOOPT);
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_sockname() {
    int sock = -1;
    bool result = false;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    printf("getsockname after bind returns bound addr...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_SOCKNAME);
    addr.sin_addr.s_addr = inet_addr(CLIENT_IP);
    EXPECT_OK(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    EXPECT_OK(getsockname(sock, (struct sockaddr *)&bound_addr, &bound_len) == 0);
    EXPECT_OK(bound_addr.sin_port == htons(TEST_PORT_SOCKNAME));
    printf("OK\n");

    printf("getsockname with NULL sockaddr should fail with EFAULT...");
    EXPECT_ERR(getsockname(sock, NULL, &len), EFAULT);
    printf("OK\n");

    printf("getsockname with NULL addrlen should fail with EFAULT...");
    EXPECT_ERR(getsockname(sock, (struct sockaddr *)&addr, NULL), EFAULT);
    printf("OK\n");

    printf("getsockname with addrlen too small should fail with EINVAL...");
    len = sizeof(addr) - 1;
    EXPECT_ERR(getsockname(sock, (struct sockaddr *)&addr, &len), EINVAL);
    printf("OK\n");

    printf("getsockname with bad FD should fail with EBADF...");
    EXPECT_ERR(getsockname(-1, (struct sockaddr *)&addr, &len), EBADF);
    printf("OK\n");

    printf("getsockname with non-socket FD should fail with ENOTSOCK...");
    EXPECT_ERR(getsockname(STDOUT_FD, (struct sockaddr *)&addr, &len), ENOTSOCK);
    printf("OK\n");

    printf("getpeername before connect fails with ENOTCONN...");
    len = sizeof(addr);
    EXPECT_ERR(getpeername(sock, (struct sockaddr *)&addr, &len), ENOTCONN);
    printf("OK\n");

    printf("getpeername with bad FD should fail with EBADF...");
    EXPECT_ERR(getpeername(-1, (struct sockaddr *)&addr, &len), EBADF);
    printf("OK\n");

    printf("getpeername with non-socket FD should fail with ENOTSOCK...");
    EXPECT_ERR(getpeername(STDOUT_FD, (struct sockaddr *)&addr, &len), ENOTSOCK);
    printf("OK\n");

    printf("getpeername with NULL sockaddr should fail with EFAULT...");
    EXPECT_ERR(getpeername(sock, NULL, &len), EFAULT);
    printf("OK\n");

    printf("getpeername with NULL addrlen should fail with EFAULT...");
    EXPECT_ERR(getpeername(sock, (struct sockaddr *)&addr, NULL), EFAULT);
    printf("OK\n");

    printf("getpeername with addrlen too small should fail with EINVAL...");
    len = sizeof(addr) - 1;
    EXPECT_ERR(getpeername(sock, (struct sockaddr *)&addr, &len), EINVAL);
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_ppoll() {
    int sock = -1;
    bool result = false;
    struct pollfd fds[1];

    printf("ppoll NULL fds with nfds > 0 fails with EFAULT...");
    EXPECT_ERR(ppoll(NULL, 1, NULL, NULL), EFAULT);
    printf("OK\n");

    printf("ppoll nfds > MAX_FDS fails with EINVAL...");
    EXPECT_ERR(ppoll(fds, MAX_FDS + 1, NULL, NULL), EINVAL);
    printf("OK\n");

    printf("ppoll with invalid FD returns POLLNVAL...");
    fds[0].fd = 999;
    fds[0].events = POLLIN;
    EXPECT_OK(poll(fds, 1, 0) == 1);
    EXPECT_OK(fds[0].revents & POLLNVAL);
    printf("OK\n");

    printf("ppoll new socket is writable (POLLOUT)...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    fds[0].fd = sock;
    fds[0].events = POLLOUT;
    EXPECT_OK(poll(fds, 1, 0) == 1);
    EXPECT_OK(fds[0].revents & POLLOUT);
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_socket_io() {
    int sock = -1;
    bool result = false;
    char buf[16];

    printf("sendto NULL buf fails with EFAULT...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_ERR(sendto(sock, NULL, 1, 0, NULL, 0), EFAULT);
    printf("OK\n");

    printf("recvfrom NULL buf fails with EFAULT...");
    EXPECT_ERR(recvfrom(sock, NULL, 1, 0, NULL, NULL), EFAULT);
    printf("OK\n");

    printf("recvfrom on unconnected socket fails with ENOTCONN...");
    EXPECT_OK(fcntl(sock, F_SETFL, O_NONBLOCK) == 0);
    EXPECT_ERR(recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL), ENOTCONN);
    printf("OK\n");

    printf("sendto with bad FD should fail with EBADF...");
    EXPECT_ERR(sendto(-1, buf, sizeof(buf), 0, NULL, 0), EBADF);
    printf("OK\n");

    printf("sendto with non-socket FD should fail with ENOTSOCK...");
    EXPECT_ERR(sendto(STDOUT_FD, buf, sizeof(buf), 0, NULL, 0), ENOTSOCK);
    printf("OK\n");

    printf("recvfrom with bad FD should fail with EBADF...");
    EXPECT_ERR(recvfrom(-1, buf, sizeof(buf), 0, NULL, NULL), EBADF);
    printf("OK\n");

    printf("recvfrom with non-socket FD should fail with ENOTSOCK...");
    EXPECT_ERR(recvfrom(STDOUT_FD, buf, sizeof(buf), 0, NULL, NULL), ENOTSOCK);
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_socket_fcntl() {
    int sock = -1;
    bool result = false;
    struct stat st;

    printf("fcntl F_SETFL O_NONBLOCK on socket...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_OK(fcntl(sock, F_SETFL, O_NONBLOCK) == 0);
    EXPECT_OK(fcntl(sock, F_GETFL, 0) & O_NONBLOCK);
    printf("OK\n");

    printf("fstat socket returns S_IFSOCK...");
    EXPECT_OK(fstat(sock, &st) == 0);
    EXPECT_OK(S_ISSOCK(st.st_mode));
    printf("OK\n");

    result = true;
cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_connect_refused() {
    int sock = -1;
    bool result = false;
    struct sockaddr_in addr;

    printf("Attempting connect to closed port should fail with ECONNREFUSED...");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_REFUSED);
    addr.sin_addr.s_addr = inet_addr(HOST_IP);

    EXPECT_ERR(connect(sock, (struct sockaddr *)&addr, sizeof(addr)), ECONNREFUSED);
    printf("OK\n");

    close(sock);
    sock = -1;

    /* Signal server we're done with connect-refused test */
    printf("POSIX_TEST|client|INFO|Signaling server that connect-refused test complete\n");
    microkit_notify(SERVER_NTFN_CH);

    result = true;

cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_blocking_echo() {
    int sock = -1;
    bool result = false;
    struct sockaddr_in addr, local_addr, peer_addr;
    socklen_t addr_len;
    char buf[64];
    const char *msg = "PING";

    printf("POSIX_TEST|client|INFO|Waiting for server to notify that they're listening...\n");
    microkit_cothread_wait_on_channel(SERVER_NTFN_CH);
    printf("POSIX_TEST|client|INFO|Server ready, connecting...\n");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_BLOCKING);
    addr.sin_addr.s_addr = inet_addr(HOST_IP);

    printf("Connect to listening server should succeed...");
    EXPECT_OK(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    printf("OK\n");

    printf("Connect from connected socket should fail with EISCONN...");
    EXPECT_ERR(connect(sock, (struct sockaddr *)&addr, sizeof(addr)), EISCONN);
    printf("OK\n");

    addr_len = sizeof(local_addr);
    EXPECT_OK(getsockname(sock, (struct sockaddr *)&local_addr, &addr_len) == 0);
    EXPECT_OK(strcmp(inet_ntoa(local_addr.sin_addr), CLIENT_IP) == 0);

    addr_len = sizeof(peer_addr);
    EXPECT_OK(getpeername(sock, (struct sockaddr *)&peer_addr, &addr_len) == 0);
    EXPECT_OK(strcmp(inet_ntoa(peer_addr.sin_addr), HOST_IP) == 0);
    EXPECT_OK(peer_addr.sin_port == htons(TEST_PORT_BLOCKING));

    printf("POSIX_TEST|client|INFO|Blocking: Sent: %s\n", msg);
    ssize_t sent = send(sock, msg, strlen(msg), 0);
    EXPECT_OK(sent == (ssize_t)strlen(msg));

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from_addr, &from_len);
    EXPECT_OK(received > 0);
    EXPECT_OK(from_len == sizeof(from_addr));
    EXPECT_OK(from_addr.sin_family == AF_INET);
    EXPECT_OK(from_addr.sin_addr.s_addr == inet_addr(HOST_IP));
    EXPECT_OK(from_addr.sin_port == htons(TEST_PORT_BLOCKING));

    buf[received] = '\0';
    EXPECT_OK(strcmp(buf, msg) == 0);
    printf("POSIX_TEST|client|INFO|Blocking: Received echo: %s\n", buf);

    result = true;

cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_nonblocking_echo() {
    int sock = -1;
    bool result = false;
    struct sockaddr_in addr;
    char buf[64];
    const char *msg = "PING_NB";
    int retry;

    printf("POSIX_TEST|client|INFO|Non-blocking: Waiting for server notification...\n");
    microkit_cothread_wait_on_channel(SERVER_NTFN_CH);
    printf("POSIX_TEST|client|INFO|Non-blocking: Server ready, connecting...\n");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);

    EXPECT_OK(fcntl(sock, F_SETFL, O_NONBLOCK) == 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_NONBLOCK);
    addr.sin_addr.s_addr = inet_addr(HOST_IP);

    struct pollfd pfd = { .fd = sock, .events = POLLOUT };

    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        EXPECT_OK(errno == EINPROGRESS);
        for (retry = 0; retry < 1000; retry++) {
            int ret = poll(&pfd, 1, 0);
            if (ret > 0 && (pfd.revents & POLLOUT)) {
                // Verify connection succeeded via SO_ERROR
                int err = 0;
                socklen_t len = sizeof(err);
                EXPECT_OK(getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) == 0);
                EXPECT_OK(err == 0);
                printf("POSIX_TEST|client|INFO|Non-blocking: Connected after %d retries\n", retry);
                break;
            }
            microkit_cothread_yield();
        }
        EXPECT_OK(retry < 1000);
    } else {
        printf("POSIX_TEST|client|INFO|Non-blocking: Connected immediately\n");
    }

    ssize_t sent;
    pfd.fd = sock;
    pfd.events = POLLOUT;
    for (retry = 0; retry < 1000; retry++) {
        int ret = poll(&pfd, 1, 0);
        if (ret > 0 && (pfd.revents & POLLOUT)) {
            sent = send(sock, msg, strlen(msg), 0);
            if (sent > 0) {
                EXPECT_OK(sent == (ssize_t)strlen(msg));
                printf("POSIX_TEST|client|INFO|Non-blocking: Sent: %s\n", msg);
                break;
            }
        }
        microkit_cothread_yield();
    }
    EXPECT_OK(retry < 1000);

    pfd.events = POLLIN;
    for (retry = 0; retry < 1000; retry++) {
        int ret = poll(&pfd, 1, 0);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            ssize_t received = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from_addr, &from_len);
            if (received > 0) {
                EXPECT_OK(from_len == sizeof(from_addr));
                EXPECT_OK(from_addr.sin_family == AF_INET);
                EXPECT_OK(from_addr.sin_addr.s_addr == inet_addr(HOST_IP));
                EXPECT_OK(from_addr.sin_port == htons(TEST_PORT_NONBLOCK));

                buf[received] = '\0';
                printf("POSIX_TEST|client|INFO|Non-blocking: Received echo: %s\n", buf);
                EXPECT_OK(strcmp(buf, msg) == 0);
                break;
            }
        }
        microkit_cothread_yield();
    }
    EXPECT_OK(retry < 1000);

    // let the server know we've completed the test
    microkit_notify(SERVER_NTFN_CH);

    result = true;

cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

void run_tests(void) {
    printf("POSIX_TEST|client|START\n");

    if (!test_socket()) {
        return;
    }
    if (!test_bind()) {
        return;
    }
    if (!test_connect()) {
        return;
    }
    if (!test_sockopt()) {
        return;
    }
    if (!test_sockname()) {
        return;
    }
    if (!test_ppoll()) {
        return;
    }
    if (!test_socket_io()) {
        return;
    }
    if (!test_socket_fcntl()) {
        return;
    }

    if (!test_connect_refused()) {
        return;
    }
    if (!test_blocking_echo()) {
        return;
    }
    if (!test_nonblocking_echo()) {
        return;
    }

    printf("POSIX_TEST|client|PASS\n");
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
        printf("POSIX_TEST|client|INFO|Waiting for DHCP...\n");
        while (!dhcp_ready) {
            microkit_cothread_yield();
        }
        printf("POSIX_TEST|client|INFO|DHCP ready, running tests\n");

        run_tests();
    } else {
        printf("POSIX_TEST|client|SKIP|Network not enabled\n");
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
        printf("POSIX_TEST|client|ERROR|Cannot initialise cothread\n");
        assert(false);
    }

    sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
    microkit_cothread_yield();
}
