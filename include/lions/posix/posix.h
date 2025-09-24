/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define MAX_SOCKET_FDS 100
#define SERVICES_FD 101

void libc_init(void);
int socket_index_of_fd(int fd);
