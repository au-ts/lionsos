/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once
#include <microkit.h>
#include <lions/posix/pager_mem.h>

void allocator_init(void);

long pager_mem_call(microkit_msginfo msginfo, microkit_child child);
