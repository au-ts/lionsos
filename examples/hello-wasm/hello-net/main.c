/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "socket_utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

static void init_sockaddr_inet(struct sockaddr_in *addr) {
    /* 0.0.0.0:1234 */
    addr->sin_family = AF_INET;
    addr->sin_port = htons(1234);
    addr->sin_addr.s_addr = htonl(INADDR_ANY);
}

int main(int argc, char *argv[]) {
    int socket_fd = -1, addrlen = 0, af;
    struct sockaddr_storage addr = {0};
    unsigned connections = 0;
    char ip_string[64];

    af = AF_INET;
    addrlen = sizeof(struct sockaddr_in6);
    init_sockaddr_inet((struct sockaddr_in *)&addr);

    printf("[Server] Create socket\n");
    socket_fd = socket(af, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Create socket failed");
        goto fail;
    }

    printf("[Server] Bind socket\n");
    if (bind(socket_fd, (struct sockaddr *)&addr, addrlen) < 0) {
        perror("Bind failed");
        goto fail;
    }

    printf("[Server] Listening on socket\n");
    if (listen(socket_fd, 3) < 0) {
        perror("Listen failed");
        goto fail;
    }

    printf("[Server] Wait for clients to connect ..\n");
    addrlen = sizeof(struct sockaddr);

    for (int i = 0; i < 3; i++) {
        int new_socket = accept(socket_fd, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            perror("Accept failed");
        }

        if (sockaddr_to_string((struct sockaddr *)&addr, ip_string, sizeof(ip_string) / sizeof(ip_string[0])) != 0) {
            printf("[Server] failed to parse client address\n");
            goto fail;
        }

        printf("[Server] Client connected (%s)\n", ip_string);

        const char *message = "Say Hi from the Server\n";
        int i;

        if (send(new_socket, message, strlen(message), 0) < 0) {
            perror("Send failed");
        }

        printf("[Server] Shutting down the new connection #%u ..\n", new_socket);
        shutdown(new_socket, SHUT_RDWR);
    }

    printf("[Server] Shutting down ..\n");
    shutdown(socket_fd, SHUT_RDWR);
    sleep(3);
    printf("[Server] BYE \n");
    return EXIT_SUCCESS;

fail:
    printf("[Server] Shutting down ..\n");
    if (socket_fd >= 0) close(socket_fd);
    sleep(3);
    return EXIT_FAILURE;
}
