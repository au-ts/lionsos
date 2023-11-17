/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>

#include <stdio.h>
#include <stdint.h>

#define STAT 0x98
#define TRANSMIT 0x40
#define STAT_TDRE (1 << 14)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (!!(x))
#define unlikely(x) (!!(x))
#endif

#define MASK(x) ((1 << (x)) - 1)
#define BIT(x) (1 << (x))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ABS(a) ((a) < 0) ? -(a) : (a)

#define dlogp(pred, fmt, ...) do { \
    if (pred) { \
        printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } \
} while (0);

#define dlog(fmt, ...) do { \
    printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
} while (0);
