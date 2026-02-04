/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>
#include <stdlib.h>
#include <stdbool.h>

#define TEST_COMPONENT "core"
#include "test_helpers.h"

static bool test_memory() {
    bool result = false;

    printf("sbrk(0) returns current break...");
    EXPECT_OK(sbrk(0) != MAP_FAILED);
    printf("OK\n");

    printf("Calling mmap(MAP_ANONYMOUS)...");
    EXPECT_OK((int)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED);
    printf("OK\n");

    printf("mmap(length=0) fails with EINVAL...");
    EXPECT_ERR((int)mmap(NULL, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), EINVAL);
    printf("OK\n");

    // WASI does not support non-anonymous mmap so returns ENOSYS instead of our ENOMEM stub
    printf("mmap(non-anonymous) fails with ENOSYS...");
    EXPECT_ERR((int)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE, 0, 0), ENOSYS);
    printf("OK\n");

    fflush(stdout);
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
    EXPECT_OK(ts2.tv_nsec >= ts1.tv_nsec || ts2.tv_sec > ts1.tv_sec);
    printf("OK\n");

    printf("Invalid clock_id returns EINVAL...");
    EXPECT_ERR(clock_gettime((clockid_t)1234, &ts1), EINVAL);
    printf("OK\n");

    result = true;

cleanup:
    return result;
}

static bool test_random() {
    bool result = false;
    char buf[16];
    memset(buf, 0, sizeof(buf));

    // WASI doesn't provide getrandom, so we test indirectly via getentropy
    printf("getentropy(buf, 16) succeeds...");
    EXPECT_OK(getentropy(buf, sizeof(buf)) == 0);
    printf("OK\n");

    result = true;

cleanup:
    return result;
}

static bool test_sleep() {
    struct timespec ts1, ts2, req;
    uint64_t diff_ns;

    printf("nanosleep(1ms) blocks for at least 1ms...");
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    req.tv_sec = 0;
    req.tv_nsec = 1000000; // 1ms
    EXPECT_OK(nanosleep(&req, NULL) == 0);
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    diff_ns = (ts2.tv_sec - ts1.tv_sec) * 1000000000ULL + (ts2.tv_nsec - ts1.tv_nsec);
    EXPECT_OK(diff_ns >= 1000000);
    printf("OK\n");

    printf("usleep(1000) blocks for at least 1ms...");
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    EXPECT_OK(usleep(1000) == 0);
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    diff_ns = (ts2.tv_sec - ts1.tv_sec) * 1000000000ULL + (ts2.tv_nsec - ts1.tv_nsec);
    EXPECT_OK(diff_ns >= 1000000);
    printf("OK\n");

    return true;

cleanup:
    return false;
}

void run_tests(void) {
    printf("WASM_TEST|core|START\n");

    if (!test_memory()) {
        return;
    }

    if (!test_clock()) {
        return;
    }

    if (!test_sleep()) {
        return;
    }

    if (!test_random()) {
        return;
    }

    printf("WASM_TEST|core|PASS\n");
}

int main(void) {
    run_tests();
    return 0;
}
