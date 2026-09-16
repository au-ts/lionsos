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
void *block_alloc(size_t num_blocks, void *heap, uint8_t *bitmap);

/**
 * Free num_blocks previously allocated by block_alloc().
 */
void block_free(void *ptr, size_t num_blocks, void *heap, uint8_t *bitmap);

#endif
