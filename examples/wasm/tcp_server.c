/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

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

int main() {
    int socket_fd = -1, addrlen = 0, af;
    struct sockaddr_storage addr = { 0 };
    char ip_string[64];

    af = AF_INET;
    addrlen = sizeof(struct sockaddr_in);
    init_sockaddr_inet((struct sockaddr_in *)&addr);

    printf("TCP_SERVER|INFO: Create socket\n");
    socket_fd = socket(af, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        printf("TCP_SERVER|ERROR: Create socket failed");
        goto fail;
    }

    printf("TCP_SERVER|INFO: Bind socket\n");
    if (bind(socket_fd, (struct sockaddr *)&addr, addrlen) < 0) {
        printf("TCP_SERVER|ERROR: Bind failed");
        goto fail;
    }

    printf("TCP_SERVER|INFO: Listening on socket\n");
    if (listen(socket_fd, 10) < 0) {
        printf("TCP_SERVER|ERROR: Listen failed");
        goto fail;
    }

    printf("TCP_SERVER|INFO: Wait for client to connect ..\n");
    addrlen = sizeof(struct sockaddr);

    int new_socket = accept(socket_fd, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
    if (new_socket < 0) {
        printf("TCP_SERVER|ERROR: Accept failed");
        goto fail;
    }

    if (sockaddr_to_string((struct sockaddr *)&addr, ip_string, sizeof(ip_string) / sizeof(ip_string[0])) != 0) {
        printf("TCP_SERVER|INFO: failed to parse client address\n");
        goto fail;
    }

    printf("TCP_SERVER|INFO: Client connected (%s), fd %d\n", ip_string, new_socket);

    const char *message = "Hi from the Server\n";

    if (send(new_socket, message, strlen(message), 0) < 0) {
        printf("TCP_SERVER|ERROR: Send failed");
    }

    printf("TCP_SERVER|INFO: Shutting down connection fd %d ..\n", new_socket);
    shutdown(new_socket, SHUT_RDWR);

    printf("TCP_SERVER|INFO: Shutting down ..\n");
    shutdown(socket_fd, SHUT_RDWR);
    printf("TCP_SERVER|INFO: BYE \n");
    return 0;

fail:
    printf("TCP_SERVER|ERROR: Shutting down ..\n");
    if (socket_fd >= 0)
        close(socket_fd);
    return -1;
}
