/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>

#include <sddf/util/printf.h>

#define dlogp(pred, fmt, ...) do { \
    if (pred) { \
        sddf_printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } \
} while (0);

#define dlog(fmt, ...) do { \
    sddf_printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
} while (0);
