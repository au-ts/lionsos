/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "bitmap.h"

#include <stdbool.h>


static inline bool block_is_allocated(size_t block, uint8_t *bitmap)
{
    return bitmap[block / 8] & (1u << (block % 8));
}


static inline void block_set(size_t block, uint8_t *bitmap)
{
    bitmap[block / 8] |= (1u << (block % 8));
}


static inline void block_clear(size_t block, uint8_t *bitmap)
{
    bitmap[block / 8] &= ~(1u << (block % 8));
}


void *block_alloc(size_t num_blocks, void *heap, uint8_t *bitmap)
{
    if (num_blocks == 0 || num_blocks > NUM_BLOCKS)
        return NULL;

    size_t run_start = 0;
    size_t run_length = 0;

    for (size_t i = 0; i < NUM_BLOCKS; i++) {

        if (!block_is_allocated(i, bitmap)) {
            if (run_length == 0)
                run_start = i;

            run_length++;

            if (run_length == num_blocks) {
                for (size_t j = run_start;
                     j < run_start + num_blocks;
                     j++) {
                    block_set(j, bitmap);
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
        block_clear(i, bitmap);
    }
}
