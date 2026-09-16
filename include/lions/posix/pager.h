/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

/*
 * PPC mr0s for syscalls redirected to pager.
 */
#define PAGER_MEM_BRK 0
#define PAGER_MEM_MMAP 1
#define PAGER_MEM_MUNMAP 2
#define PAGER_MEM_FORK 3
