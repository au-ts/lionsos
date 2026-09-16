/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "mem.h"
#include "bitmap.h"
#include "page_table.h"
#include "pager.h"
#include "proc.h"

#include <errno.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>

/**
 * I need to create an allocator with these memory regions.
 */
// set to 0x8000000000
static void *heaps[MAX_CHILDREN];
// set to 0x7000000000
static void *brks[MAX_CHILDREN];
static void *morecore_bases[MAX_CHILDREN];

static uint8_t bitmaps[MAX_CHILDREN][BITMAP_SIZE];

/**
 * Initialise the allocator.
 */
void allocator_init()
{
    for (int i = 0; i < MAX_CHILDREN; ++i) {
        memset(bitmaps[i], 0, BITMAP_SIZE);
        heaps[i] = (void *)0x8000000000ULL;
        brks[i] = (void *)0x7000000000ULL;
        morecore_bases[i] = (void *)0x7000000000ULL;
    }
}

/**
 * TODO: implement bookkeeping of allocated memory regions so that pager can validate.
 */

static long sys_brk(uintptr_t newbrk, microkit_child child) {
    if (newbrk <= 0x800000000 && newbrk >= (uintptr_t)morecore_bases[child]) {
        if (newbrk < (uintptr_t)brks[child]) {
            unmap_range(ROUND_DOWN_TO_4K(brks[child]), ROUND_DOWN_TO_4K(newbrk), child);
        }
        brks[child] = (void *)newbrk;
        return (long)newbrk;
    }
    return (long)brks[child];
}

static long sys_mmap(size_t length, int flags, microkit_child child) {
    if (length == 0 || !(flags & MAP_ANONYMOUS)) {
        return -ENOMEM;
    }
    // return an address that is n blocks long.
    length = ROUND_UP_TO_4K(length);
    uintptr_t addr = (uintptr_t)block_alloc(length / PAGE_SIZE, heaps[child], bitmaps[child]);
    if (!addr) {
        // A NULL return would be read back as a successful mapping at 0.
        return -ENOMEM;
    }
    return (long)addr;
}

static long sys_munmap(uintptr_t addr, size_t length, microkit_child child) {
    length = ROUND_UP_TO_4K(length);
    if (length == 0) {
        return -EINVAL;
    }
    block_free((void *)ROUND_DOWN_TO_4K(addr), length / PAGE_SIZE,
               heaps[child], bitmaps[child]);
    unmap_range(ROUND_DOWN_TO_4K(addr), ROUND_DOWN_TO_4K(addr) + length, child);
    return 0;
}

/**
 * The client's libc does not have a PAGER_MEM_MPROTECT to send yet, every
 * mapping the pager makes is already read/write.
 */
static long sys_mprotect(uintptr_t addr, size_t size, int prot) {
    (void)addr, (void)size, (void)prot;
    // do nothing.
    return 0;
}

static long sys_fork(microkit_child child) {
    long new_child = pager_fork(child);
    if (new_child > 0 && new_child < MAX_CHILDREN) {
        brks[new_child] = brks[child];
        morecore_bases[new_child] = morecore_bases[child];
        memcpy(bitmaps[new_child], bitmaps[child], sizeof(bitmaps[child]));
    }
    return new_child;
}

long pager_mem_call(microkit_msginfo msginfo, microkit_child child)
{
    switch (microkit_msginfo_get_label(msginfo)) {
    case PAGER_MEM_BRK:
        return sys_brk(microkit_mr_get(0), child);
    case PAGER_MEM_MMAP:
        return sys_mmap(microkit_mr_get(1), microkit_mr_get(3), child);
    case PAGER_MEM_MUNMAP:
        return sys_munmap(microkit_mr_get(0), microkit_mr_get(1), child);
    case PAGER_MEM_FORK:
        return sys_fork(child);
    default:
        return -ENOSYS;
    }
}
