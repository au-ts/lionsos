/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdarg.h>
#include <bits/syscall.h>
#include <stdint.h>

#define SERVICES_FD 101

#define MUSLC_HIGHEST_SYSCALL SYS_pkey_free
#define MUSLC_NUM_SYSCALLS (MUSLC_HIGHEST_SYSCALL + 1)

typedef long (*muslcsys_syscall_t)(va_list);

void libc_init(void);
int socket_index_of_fd(int fd);
void libc_define_syscall(int syscall_num, muslcsys_syscall_t syscall_func);
