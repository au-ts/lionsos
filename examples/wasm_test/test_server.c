/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

#define TEST_COMPONENT "server"
#include "test_helpers.h"

#define TEST_PORT 5555
#define TEST_PORT_BLOCKING 5560
#define TEST_PORT_NONBLOCK 5561

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

    // WASI fails this with ENOTCAPABLE instead of ENOTSOCK
    printf("Listen on file FD fails with ENOTCAPABLE...");
    EXPECT_ERR(listen(STDOUT_FILENO, 5), ENOTCAPABLE);
    printf("OK\n");

    printf("Listen same port twice fails with EADDRINUSE...");
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

    // WASI fails this with ENOTCAPABLE instead of ENOTSOCK
    printf("Accept on file FD fails with ENOTCAPABLE...");
    EXPECT_ERR(accept(STDOUT_FILENO, NULL, NULL), ENOTCAPABLE);
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
    EXPECT_OK(fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK) == 0);
    EXPECT_ERR(accept(sock, NULL, NULL), EAGAIN);
    printf("OK\n");

    printf("Accept on non-listening socket fails with EINVAL...");
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

    printf("WASM_TEST|server|INFO|Setting up server...\n");

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
    printf("WASM_TEST|server|INFO|Listening on %d\n", ntohs(local_addr.sin_port));

    printf("Accept connecting client should succeed...");
    addr_len = sizeof(peer_addr);
    client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
    EXPECT_OK(client_fd >= 0);
    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, ip_buf, sizeof(ip_buf));
    printf("OK (Client: %s, fd: %d)\n", ip_buf, client_fd);

    ssize_t received = recv(client_fd, buf, sizeof(buf) - 1, 0);
    EXPECT_OK(received > 0);
    buf[received] = '\0';
    printf("WASM_TEST|server|INFO|Blocking: Received: %s\n", buf);
    EXPECT_OK(strcmp(buf, expected) == 0);

    ssize_t sent = send(client_fd, buf, received, 0);
    EXPECT_OK(sent == received);
    printf("WASM_TEST|server|INFO|Blocking: Echoed: %s\n", buf);

    result = true;

cleanup:
  // Wait for client to close first
    if (client_fd >= 0) {
        ssize_t r;
        int wait_retries = 0;
        while (wait_retries++ < 5000) {
            r = recv(client_fd, buf, sizeof(buf), 0);
            if (r == 0 || (r < 0 && errno == ENOTCONN)) {
                break;
            }
            usleep(1000);
        }
    }
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

    printf("WASM_TEST|server|INFO|Non-blocking: Setting up server...\n");
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_OK(listen_fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PORT_NONBLOCK);
    addr.sin_addr.s_addr = INADDR_ANY;
    EXPECT_OK(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    EXPECT_OK(listen(listen_fd, 5) == 0);

    EXPECT_OK(fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL) | O_NONBLOCK) == 0);
    printf("WASM_TEST|server|INFO|Non-blocking: Listening\n");

    printf("WASM_TEST|server|INFO|Non-blocking: Waiting for client to connect...\n");
    for (retry = 0; retry < 10000; retry++) {
        addr_len = sizeof(peer_addr);
        client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
        if (client_fd >= 0) {
            break;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            EXPECT_OK(false);
        }
        usleep(1000); // Sleep 1ms between attempts
    }
    EXPECT_OK(client_fd >= 0);
    printf("WASM_TEST|server|INFO|Non-blocking: Accepted after %d retries\n", retry);

    EXPECT_OK(fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK) == 0);

    for (retry = 0; retry < 10000; retry++) {
        ssize_t received = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (received > 0) {
            buf[received] = '\0';
            printf("WASM_TEST|server|INFO|Non-blocking: Received: %s\n", buf);
            EXPECT_OK(strcmp(buf, expected) == 0);

            ssize_t sent = send(client_fd, buf, received, 0);
            EXPECT_OK(sent == received);
            printf("WASM_TEST|server|INFO|Non-blocking: Echoed: %s\n", buf);
            break;
        }
        EXPECT_OK(errno == EAGAIN || errno == EWOULDBLOCK);
        usleep(1000); // Sleep 1ms between attempts
    }
    EXPECT_OK(retry < 10000);

    result = true;

cleanup:
  // Wait for client to close first
    if (client_fd >= 0) {
        ssize_t r;
        int wait_retries = 0;
        while (wait_retries++ < 5000) {
            r = recv(client_fd, buf, sizeof(buf), 0);
            if (r == 0 || (r < 0 && errno == ENOTCONN)) {
                break;
            }
            usleep(1000);
        }
    }
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    return result;
}

void run_tests(void) {
    printf("WASM_TEST|server|START\n");

    if (!test_listen()) {
        return;
    }
    if (!test_accept()) {
        return;
    }

    if (!test_blocking_echo()) {
        return;
    }
    if (!test_nonblocking_echo()) {
        return;
    }

    printf("WASM_TEST|server|PASS\n");
}

int main(void) {
    run_tests();
    return 0;
}
