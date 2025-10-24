/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <stdarg.h>
#include <bits/syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <autoconf.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/types.h>

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>

#include <lions/posix/posix.h>
#include <lions/util.h>

extern void *__sysinfo;
static muslcsys_syscall_t syscall_table[MUSLC_NUM_SYSCALLS] = {0};

long sys_clock_gettime(va_list ap) {
    clockid_t clk_id = va_arg(ap, clockid_t);
    (void)clk_id;
    struct timespec *tp = va_arg(ap, struct timespec *);

    uint64_t rtc = 0;

    tp->tv_sec = rtc / 1000;
    tp->tv_nsec = (rtc % 1000) * 1000000;

    return 0;
}

long sys_getpid(va_list ap) { return 0; }

long sys_getuid(va_list ap) {
    (void)ap;
    return 501;
}

long sys_getgid(va_list ap) {
    (void)ap;
    return 501;
}

long sys_getrandom(va_list ap) {
    void *buf = va_arg(ap, void *);
    size_t buflen = va_arg(ap, size_t);
    unsigned flags = va_arg(ap, unsigned);

    (void)flags;

    size_t bytes_written = 0;

    while (bytes_written < buflen) {
        int r = rand();

        size_t bytes_to_copy = sizeof(int);
        if (bytes_written + bytes_to_copy > buflen) {
            bytes_to_copy = buflen - bytes_written;
        }

        memcpy(buf + bytes_written, &r, bytes_to_copy);
        bytes_written += bytes_to_copy;
    }

    return bytes_written;
}

void debug_error(long num) { dlog("error doing syscall: %d", num); }

int pthread_setcancelstate(int state, int *oldstate) {
    (void)state;
    (void)oldstate;
    return 0;
}

long sel4_vsyscall(long sysnum, ...) {
    va_list al;
    va_start(al, sysnum);
    muslcsys_syscall_t syscall;

    if (sysnum < 0 || sysnum >= ARRAY_SIZE(syscall_table)) {
        debug_error(sysnum);
        return -ENOSYS;
    } else {
        syscall = syscall_table[sysnum];
    }
    /* Check a syscall is implemented there */
    if (!syscall) {
        debug_error(sysnum);
        return -ENOSYS;
    }
    /* Call it */
    long ret = syscall(al);
    va_end(al);
    return ret;
}

void libc_define_syscall(int syscall_num, muslcsys_syscall_t syscall_func) {
    assert(syscall_num >= 0 && syscall_num < ARRAY_SIZE(syscall_table));
    assert(syscall_table[syscall_num] == NULL);
    syscall_table[syscall_num] = syscall_func;
}

void libc_init_mem();
void libc_init_io();
void libc_init_file();
void libc_init_sock(libc_socket_config_t *);

void libc_init(libc_socket_config_t *socket_config) {
    /* Syscall table init */
    __sysinfo = sel4_vsyscall;
    libc_init_mem();
    libc_init_io();
    libc_init_file();

    if (socket_config != NULL) {
        libc_init_sock(socket_config);
    }

    syscall_table[__NR_getpid] = sys_getpid;
    syscall_table[__NR_clock_gettime] = sys_clock_gettime;
    syscall_table[__NR_getuid] = sys_getuid;
    syscall_table[__NR_getgid] = sys_getgid;
    syscall_table[__NR_getrandom] = sys_getrandom;
}
