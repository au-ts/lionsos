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
#include <sddf/timer/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/protocol.h>

#include <lions/posix/posix.h>

//TODO: fix to use header; have issue with providing correct libmicrokitco_opts.h
void microkit_cothread_wait_on_channel(const microkit_channel wake_on);

extern size_t __sysinfo;
extern timer_client_config_t timer_config;
static muslcsys_syscall_t syscall_table[MUSLC_NUM_SYSCALLS] = { 0 };

static long sys_clock_gettime(va_list ap) {
    clockid_t clk_id = va_arg(ap, clockid_t);
    struct timespec *tp = va_arg(ap, struct timespec *);

    // we alias CLOCK_REALTIME to CLOCK_MONOTONIC for now
    if (clk_id != CLOCK_MONOTONIC && clk_id != CLOCK_REALTIME) {
        return -EINVAL;
    }

    if (tp == NULL) {
        return -EFAULT;
    }

    uint64_t rtc = sddf_timer_time_now(timer_config.driver_id);

    tp->tv_sec = rtc / NS_IN_S;
    tp->tv_nsec = rtc % NS_IN_S;

    return 0;
}

static long sys_nanosleep(va_list ap) {
    const struct timespec *req = va_arg(ap, const struct timespec *);
    struct timespec *rem = va_arg(ap, struct timespec *);

    if (req == NULL) {
        return -EFAULT;
    }

    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= NS_IN_S) {
        return -EINVAL;
    }

    uint64_t sleep_ns = (uint64_t)req->tv_sec * NS_IN_S + (uint64_t)req->tv_nsec;
    uint64_t start_time = sddf_timer_time_now(timer_config.driver_id);
    uint64_t target_time = start_time + sleep_ns;

    sddf_timer_set_timeout(timer_config.driver_id, sleep_ns);

    // Loop until target time reached
    // May wake spuriously due to timer expiring for other reasons
    while (sddf_timer_time_now(timer_config.driver_id) < target_time) {
        microkit_cothread_wait_on_channel(timer_config.driver_id);
    }

    // No signal interruption support - always sleep full duration
    if (rem != NULL) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

static long sys_getpid(va_list ap) {
    (void)ap;
    return 0;
}

static long sys_getuid(va_list ap) {
    (void)ap;
    return 501;
}

static long sys_getgid(va_list ap) {
    (void)ap;
    return 501;
}

// FIXME: this is deliberately insecure for now
static long sys_getrandom(va_list ap) {
    void *buf = va_arg(ap, void *);
    size_t buflen = va_arg(ap, size_t);
    unsigned flags = va_arg(ap, unsigned);

    (void)flags;

    if (buf == NULL) {
        return -EFAULT;
    }

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

static long sel4_vsyscall(long sysnum, ...) {
    va_list al;
    va_start(al, sysnum);
    muslcsys_syscall_t syscall;
    long ret;

    if (sysnum < 0 || sysnum >= ARRAY_SIZE(syscall_table)) {
        fprintf(stderr, "POSIX|ERROR: Invalid syscall number: %ld\n", sysnum);
        ret = -ENOSYS;
        goto cleanup;
    } else {
        syscall = syscall_table[sysnum];
    }

    /* Check a syscall is implemented there */
    if (!syscall) {
        fprintf(stderr, "POSIX|ERROR: Unimplemented syscall number: %ld\n", sysnum);
        ret = -ENOSYS;
        goto cleanup;
    }

    /* Call it */
    ret = syscall(al);

cleanup:
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
    __sysinfo = (size_t)sel4_vsyscall;
    libc_init_mem();
    libc_init_io();
    libc_init_file();

    if (socket_config != NULL) {
        libc_init_sock(socket_config);
    }

    libc_define_syscall(__NR_clock_gettime, sys_clock_gettime);
    libc_define_syscall(__NR_nanosleep, sys_nanosleep);
    libc_define_syscall(__NR_getpid, sys_getpid);
    libc_define_syscall(__NR_getuid, sys_getuid);
    libc_define_syscall(__NR_getgid, sys_getgid);
    libc_define_syscall(__NR_getrandom, sys_getrandom);
}
