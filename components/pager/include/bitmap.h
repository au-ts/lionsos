/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _BITMAP_H
#define _BITMAP_H

#include <stddef.h>
#include <stdint.h>

#define BLOCK_SIZE 4096
/* Covers the 0x20000000 mmap arena the client hands to libc_init(). */
#define NUM_BLOCKS 131072

/**
 * Size of the bitmap a heap of NUM_BLOCKS blocks needs. One bit per block:
 *   0 = free
 *   1 = allocated
 */
#define BITMAP_SIZE ((NUM_BLOCKS + 7) / 8)

/**
 * Allocate num_blocks contiguous BLOCK_SIZE-byte blocks.
 *
 * Returns:
 *
 *     heap + offset
 *
 * or NULL if no sufficiently large contiguous region exists.
 */
void *block_alloc(size_t num_blocks, void *heap, uint8_t *bitmap)
{
    if (num_blocks == 0 || num_blocks > NUM_BLOCKS)
        return NULL;

    size_t run_start = 0;
    size_t run_length = 0;

    for (size_t i = 0; i < NUM_BLOCKS; i++) {

        if (!(bitmap[i / 8] & (1u << (i % 8)))) {
            if (run_length == 0)
                run_start = i;

            run_length++;

            if (run_length == num_blocks) {
                for (size_t j = run_start;
                     j < run_start + num_blocks;
                     j++) {
                    bitmap[j / 8] |= (1u << (j % 8));
                }

                return (uint8_t *)heap +
                       run_start * BLOCK_SIZE;
            }

        } else {
            run_length = 0;
        }
    }

    return NULL;
}

/**
 * Free num_blocks previously allocated by block_alloc().
 */
void block_free(void *ptr, size_t num_blocks, void *heap, uint8_t *bitmap)
{
    if (ptr == NULL || num_blocks == 0)
        return;

    uintptr_t start = (uintptr_t)heap;
    uintptr_t addr  = (uintptr_t)ptr;

    /*
     * Pointer must be inside heap.
     */
    if (addr < start)
        return;

    uintptr_t offset = addr - start;

    /*
     * Allocation must start on a 4096-byte boundary.
     */
    if (offset % BLOCK_SIZE != 0)
        return;

    size_t block = offset / BLOCK_SIZE;

    if (block >= NUM_BLOCKS ||
        num_blocks > NUM_BLOCKS - block)
        return;

    for (size_t i = block;
         i < block + num_blocks;
         i++) {
        bitmap[i / 8] &= ~(1u << (i % 8));
    }
}

#endif
