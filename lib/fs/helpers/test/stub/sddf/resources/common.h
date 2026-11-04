/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Host stand-in for the sDDF resource types the fs config uses.
 */
#pragma once

#include <stdint.h>

typedef struct region_resource {
    void *vaddr;
    uint64_t size;
} region_resource_t;
