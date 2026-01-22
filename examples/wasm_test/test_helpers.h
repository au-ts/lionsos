/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/*
 * Test helper macros for POSIX syscall tests.
 *
 * Usage:
 *   #define TEST_COMPONENT "client"
 *   #include "test_helpers.h"
 *
 * Each test function should use goto cleanup pattern:
 *
 *   static bool test_foo() {
 *       int fd = -1;
 *       bool result = false;
 *
 *       printf("description ...");
 *       fd = open(...);
 *       EXPECT_OK(fd >= 0);
 *       printf("OK\n");
 *
 *       result = true;
 *   cleanup:
 *       if (fd >= 0) close(fd);
 *       return result;
 *   }
 */

#ifndef TEST_COMPONENT
#define TEST_COMPONENT "unknown"
#endif

/* Check condition is true, goto cleanup if false */
#define EXPECT_OK(expr) do { \
    if (!(expr)) { \
        printf("FAILED: %s, errno = %d\n", #expr, errno); \
        goto cleanup; \
    } \
} while (0)

/* Expect expression < 0 with specific errno */
#define EXPECT_ERR(expr, expected_errno) do { \
    if ((expr) >= 0) { \
        printf("FAILED: %s should have failed\n", #expr); \
        goto cleanup; \
    } \
    if (errno != (expected_errno)) { \
        printf("FAILED: expected %s (%d), got %d\n", \
               #expected_errno, (expected_errno), errno); \
        goto cleanup; \
    } \
} while (0)
