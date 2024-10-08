/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>

#define SOCKET_BUF_SIZE 0x200000ll
#define MAX_SOCKETS 3

void tcp_init_0(void);
int tcp_ready(void);
void tcp_update(void);
void tcp_process_rx(void);
void tcp_maybe_notify(void);

int tcp_socket_create(void);
int tcp_socket_connect(int index, int port);
int tcp_socket_close(int index);
int tcp_socket_dup(int index_old, int index_new);
int tcp_socket_write(int index, const char *buf, size_t len);
ssize_t tcp_socket_recv(int index, char *buf, ssize_t len);
int tcp_socket_readable(int index);
int tcp_socket_writable(int index);
int tcp_socket_hup(int index);
int tcp_socket_err(int index);
