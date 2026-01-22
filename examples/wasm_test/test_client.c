/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

#define TEST_COMPONENT "client"
#include "test_helpers.h"

#define TEST_PORT_BIND 5556
#define TEST_PORT_CONNECT 5557
#define TEST_PORT_SOCKNAME 5558
#define TEST_PORT_REFUSED 5559
#define TEST_PORT_BLOCKING 5560
#define TEST_PORT_NONBLOCK 5561

#define HOST_IP "10.0.2.2"

static bool test_socket() {
    int sock = -1;
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

    printf("Create SOCK_DGRAM socket should fail...");
    EXPECT_OK(socket(AF_INET, SOCK_DGRAM, 0) < 0);
    printf("OK\n");

    result = true;

cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

static bool test_bind() {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_BIND);
    addr.sin_addr.s_addr = INADDR_ANY;

    int sock = -1;
    int sock2 = -1;
    bool result = false;

    // WASI fails this with ENOTCAPABLE instead of ENOTSOCK
    printf("Binding to non-socket FD should fail with ENOTCAPABLE...");
    EXPECT_ERR(bind(STDOUT_FILENO, (struct sockaddr *)&addr, sizeof(addr)), ENOTCAPABLE);
    printf("OK\n");

    // WASI fails this with EAFNOSUPPORT instead of EFAULT
    printf("Binding to NULL address should fail with EAFNOSUPPORT...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_ERR(bind(sock, NULL, sizeof(addr)), EAFNOSUPPORT);
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
    inet_pton(AF_INET, HOST_IP, &addr.sin_addr);

    int sock = -1;
    bool result = false;

    printf("Connect bad FD fails with EBADF...");
    EXPECT_ERR(connect(-1, (struct sockaddr *)&addr, sizeof(addr)), EBADF);
    printf("OK\n");

    // WASI fails this with ENOTCAPABLE instead of ENOTSOCK
    printf("Connect file FD fails with ENOTCAPABLE...");
    EXPECT_ERR(connect(STDOUT_FILENO, (struct sockaddr *)&addr, sizeof(addr)), ENOTCAPABLE);
    printf("OK\n");

    // WASI fails this with EINVAL instead of EFAULT
    printf("Connect NULL addr fails with EINVAL...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_ERR(connect(sock, NULL, sizeof(addr)), EINVAL);
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
    addr.sin_addr.s_addr = INADDR_ANY;
    EXPECT_OK(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    EXPECT_OK(getsockname(sock, (struct sockaddr *)&bound_addr, &bound_len) == 0);
    EXPECT_OK(bound_addr.sin_port == htons(TEST_PORT_SOCKNAME));
    printf("OK\n");

    printf("getpeername before connect fails with ENOTCONN...");
    len = sizeof(addr);
    EXPECT_ERR(getpeername(sock, (struct sockaddr *)&addr, &len), ENOTCONN);
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

    printf("recvfrom on unconnected socket fails with ENOTCONN...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    EXPECT_ERR(recv(sock, buf, sizeof(buf), 0), ENOTCONN);
    printf("OK\n");

    printf("sendto with bad FD should fail with EBADF...");
    EXPECT_ERR(send(-1, buf, sizeof(buf), 0), EBADF);
    printf("OK\n");

    printf("recvfrom with bad FD should fail with EBADF...");
    EXPECT_ERR(recv(-1, buf, sizeof(buf), 0), EBADF);
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

    printf("fcntl F_SETFL O_NONBLOCK on socket...");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);
    EXPECT_OK(fcntl(sock, F_SETFL, O_NONBLOCK) == 0);
    EXPECT_OK(fcntl(sock, F_GETFL, 0) & O_NONBLOCK);
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
    fflush(stdout);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(sock >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_REFUSED);
    inet_pton(AF_INET, HOST_IP, &addr.sin_addr);

    EXPECT_ERR(connect(sock, (struct sockaddr *)&addr, sizeof(addr)), ECONNREFUSED);
    printf("OK\n");

    close(sock);
    sock = -1;

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
    struct sockaddr_in addr;
    char buf[64];
    const char *msg = "PING";
    int retry;

    printf("WASM_TEST|client|INFO|Connecting to server (with retry)...\n");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_BLOCKING);
    inet_pton(AF_INET, HOST_IP, &addr.sin_addr);

    for (retry = 0; retry < 1000; retry++) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            break;
        }
        close(sock);
        sock = -1;
        usleep(10000);
    }
    EXPECT_OK(sock >= 0);
    printf("WASM_TEST|client|INFO|Connected after %d retries\n", retry);

    printf("WASM_TEST|client|INFO|Blocking: Sent: %s\n", msg);
    ssize_t sent = send(sock, msg, strlen(msg), 0);
    EXPECT_OK(sent == (ssize_t)strlen(msg));

    ssize_t received = recv(sock, buf, sizeof(buf) - 1, 0);
    EXPECT_OK(received > 0);
    buf[received] = '\0';
    EXPECT_OK(strcmp(buf, msg) == 0);
    printf("WASM_TEST|client|INFO|Blocking: Received echo: %s\n", buf);
    fflush(stdout);

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

    printf("WASM_TEST|client|INFO|Non-blocking: Connecting...\n");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_NONBLOCK);
    inet_pton(AF_INET, HOST_IP, &addr.sin_addr);

    for (retry = 0; retry < 1000; retry++) {
        if (sock == -1) {
            sock = socket(AF_INET, SOCK_STREAM, 0);
            fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
        }
        int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == 0 || errno == EISCONN) {
            break;
        }
        EXPECT_OK(errno == EINPROGRESS || errno == EALREADY);
        usleep(1000);  // Sleep 1ms between attempts
    }

    EXPECT_OK(retry < 1000);
    printf("WASM_TEST|client|INFO|Non-blocking: Connected after %d retries\n", retry);

    ssize_t sent;
    for (retry = 0; retry < 1000; retry++) {
        sent = send(sock, msg, strlen(msg), 0);
        if (sent > 0) {
            EXPECT_OK(sent == (ssize_t)strlen(msg));
            printf("WASM_TEST|client|INFO|Non-blocking: Sent: %s\n", msg);
            break;
        }
        EXPECT_OK(errno == EAGAIN || errno == EWOULDBLOCK);
        usleep(1000);  // Sleep 1ms between attempts
    }
    EXPECT_OK(retry < 1000);

    for (retry = 0; retry < 1000; retry++) {
        ssize_t received = recv(sock, buf, sizeof(buf) - 1, 0);
        if (received > 0) {
            buf[received] = '\0';
            printf("WASM_TEST|client|INFO|Non-blocking: Received echo: %s\n", buf);
            EXPECT_OK(strcmp(buf, msg) == 0);
            break;
        }
        usleep(1000);  // Sleep 1ms between attempts
    }
    EXPECT_OK(retry < 1000);

    result = true;

cleanup:
    if (sock >= 0) {
        close(sock);
    }
    return result;
}

void run_tests(void) {
    printf("WASM_TEST|client|START\n");

    if (!test_socket()) {
        return;
    }
    if (!test_bind()) {
        return;
    }
    if (!test_connect()) {
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

    printf("WASM_TEST|client|PASS\n");
}

int main(void) {
    run_tests();
    return 0;
}
