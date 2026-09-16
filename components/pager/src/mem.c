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
 * The mmap arena and heap each client's libc is handed, one per client. Where they
 * start comes from the system description, so the pager and the client's libc cannot
 * disagree about it.
 */
static void *heaps[PAGER_MAX_CLIENTS];
static void *brks[PAGER_MAX_CLIENTS];
static void *morecore_bases[PAGER_MAX_CLIENTS];

static uint8_t bitmaps[PAGER_MAX_CLIENTS][BITMAP_SIZE];

/**
 * Initialise the allocator.
 */
void allocator_init(pager_server_config_t *config)
{
    for (uint8_t i = 0; i < config->num_clients; ++i) {
        memset(bitmaps[i], 0, BITMAP_SIZE);
        heaps[i] = (void *)config->clients[i].mmap_base;
        brks[i] = (void *)config->clients[i].brk_base;
        morecore_bases[i] = (void *)config->clients[i].brk_base;
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
 * every mapping the pager makes is already read/write.
 */
static long sys_mprotect(uintptr_t addr, size_t size, int prot) {
    (void)addr, (void)size, (void)prot;
    // do nothing.
    return 0;
}

static long sys_fork(microkit_child child) {
    long new_child = pager_fork(child);
    if (new_child > 0 && new_child < PAGER_MAX_CLIENTS) {
        heaps[new_child] = heaps[child];
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
