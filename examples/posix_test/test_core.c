/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>

#include <microkit.h>
#include <libmicrokitco.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/timer/protocol.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_tx_queue_handle;
serial_queue_handle_t serial_rx_queue_handle;

bool serial_rx_enabled;

#define LIBC_COTHREAD_STACK_SIZE 0x10000
static char libc_cothread_stack[LIBC_COTHREAD_STACK_SIZE];
static co_control_t co_controller_mem;

#define TEST_COMPONENT "core"
#include "test_helpers.h"

static bool test_memory() {
    bool result = false;
    void *initial_brk = sbrk(0);
    void *ptr = NULL;

    printf("sbrk(0) returns current break...");
    EXPECT_OK(initial_brk != (void *)-1);
    printf("OK\n");

    printf("mmap(MAP_ANONYMOUS) returns valid ptr...");
    ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    EXPECT_OK(ptr != MAP_FAILED);
    printf("OK\n");

    printf("mmap(length=0) fails with EINVAL...");
    ptr = mmap(NULL, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    EXPECT_OK(ptr == MAP_FAILED && errno == EINVAL);
    printf("OK\n");

    printf("mmap(non-anonymous) fails with ENOMEM...");
    ptr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE, 0, 0);
    EXPECT_OK(ptr == MAP_FAILED && errno == ENOMEM);
    printf("OK\n");

    // munmap is an intentional stub that always returns 0.
    printf("munmap always returns 0...");
    EXPECT_OK(munmap(ptr, 0x1000) == 0);
    printf("OK\n");

    // mprotect is an intentional stub that always returns 0.
    printf("mprotect always returns 0...");
    EXPECT_OK(mprotect(ptr, 0x1000, PROT_READ) == 0);
    printf("OK\n");

    result = true;
cleanup:
    return result;
}

static bool test_clock() {
    bool result = false;
    struct timespec ts1, ts2;

    printf("clock_gettime(CLOCK_MONOTONIC) succeeds...");
    EXPECT_OK(clock_gettime(CLOCK_MONOTONIC, &ts1) == 0);
    printf("OK\n");

    printf("clock_gettime(CLOCK_REALTIME) succeeds...");
    EXPECT_OK(clock_gettime(CLOCK_REALTIME, &ts2) == 0);
    printf("OK\n");

    printf("second clock call time value exceeds first...");
    EXPECT_OK(ts2.tv_nsec >= ts1.tv_nsec);
    printf("OK\n");

    printf("Invalid clock_id returns EINVAL...");
    EXPECT_ERR(clock_gettime(1234, &ts1), EINVAL);
    printf("OK\n");

    printf("NULL timespec returns EFAULT...");
    EXPECT_ERR(clock_gettime(CLOCK_MONOTONIC, NULL), EFAULT);
    printf("OK\n");

    result = true;
cleanup:
    return result;
}

static bool test_identity() {
    bool result = false;

    // getpid is an intentional stub that returns 0.
    printf("getpid() returns 0...");
    EXPECT_OK(getpid() == 0);
    printf("OK\n");

    // getuid is an intentional stub that returns 501.
    printf("getuid() returns 501...");
    EXPECT_OK(getuid() == 501);
    printf("OK\n");

    // getgid is an intentional stub that returns 501.
    printf("getgid() returns 501...");
    EXPECT_OK(getgid() == 501);
    printf("OK\n");

    result = true;
cleanup:
    return result;
}

static bool test_random() {
    bool result = false;
    char buf[16];
    memset(buf, 0, sizeof(buf));

    // getrandom is pseudorandom and always succeeds with a valid buffer.
    printf("getrandom(buf, 16) returns 16...");
    EXPECT_OK(getrandom(buf, sizeof(buf), 0) == sizeof(buf));
    printf("OK\n");

    printf("getrandom(NULL, 16) returns EFAULT...");
    EXPECT_ERR(getrandom(NULL, sizeof(buf), 0), EFAULT);
    printf("OK\n");

    result = true;
cleanup:
    return result;
}

static bool test_sleep() {
    bool result = false;
    struct timespec ts1, ts2, req, rem;
    uint64_t diff_ns;

    printf("nanosleep(1ms) blocks for at least 1ms...");
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    req.tv_sec = 0;
    req.tv_nsec = 1000000; // 1ms
    EXPECT_OK(nanosleep(&req, &rem) == 0);
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    diff_ns = (ts2.tv_sec - ts1.tv_sec) * NS_IN_S + (ts2.tv_nsec - ts1.tv_nsec);
    EXPECT_OK(diff_ns >= 1000000);
    printf("OK\n");

    printf("usleep(1ms) blocks for at least 1ms...");
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    EXPECT_OK(usleep(1000) == 0);
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    diff_ns = (ts2.tv_sec - ts1.tv_sec) * NS_IN_S + (ts2.tv_nsec - ts1.tv_nsec);
    EXPECT_OK(diff_ns >= 1000000);
    printf("OK\n");

    printf("nanosleep(NULL) returns EFAULT...");
    EXPECT_ERR(nanosleep(NULL, &rem), EFAULT);
    printf("OK\n");

    printf("nanosleep(invalid nsec) returns EINVAL...");
    req.tv_sec = 0;
    req.tv_nsec = NS_IN_S;
    EXPECT_ERR(nanosleep(&req, &rem), EINVAL);
    printf("OK\n");

    result = true;
cleanup:
    return result;
}

void run_tests(void) {
    printf("POSIX_TEST|core|START\n");

    if (!test_memory()) {
        return;
    }

    if (!test_clock()) {
        return;
    }

    if (!test_sleep()) {
        return;
    }

    if (!test_identity()) {
        return;
    }

    if (!test_random()) {
        return;
    }

    printf("POSIX_TEST|core|PASS\n");
}

void cont(void) {
    libc_init(NULL);
    run_tests();
}

void notified(microkit_channel ch) { microkit_cothread_recv_ntfn(ch); }

void init(void) {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));

    serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);
    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    stack_ptrs_arg_array_t costacks = { (uintptr_t)libc_cothread_stack };
    microkit_cothread_init(&co_controller_mem, LIBC_COTHREAD_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(cont, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("POSIX_TEST|core|ERROR|Cannot initialise cothread\n");
        assert(false);
    }

    microkit_cothread_yield();
}
