/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int socket_allocate(void);
int tcp_socket_init(int index);
int tcp_socket_connect(int index, uint32_t addr, uint16_t port);
int tcp_socket_close(int index);
int tcp_socket_dup(int index);
ssize_t tcp_socket_write(int index, const char *buf, size_t len);
ssize_t tcp_socket_recv(int index, char *buf, size_t len);
int tcp_socket_readable(int index);
int tcp_socket_writable(int index);
int tcp_socket_hup(int index);
int tcp_socket_err(int index);
int tcp_socket_listen(int index, int backlog);
int tcp_socket_accept(int index);
int tcp_socket_bind(int index, uint32_t addr, uint16_t port);
int tcp_socket_getsockname(int index, uint32_t *addr, uint16_t *port);
int tcp_socket_getpeername(int index, uint32_t *addr, uint16_t *port);
