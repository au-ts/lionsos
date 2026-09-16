/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _FRAME_TABLE_H
#define _FRAME_TABLE_H

#include <stdint.h>
#include <sel4/sel4.h>

#include "pager.h"

/**
 * TODO: support multiple mappings.
 * - currently only one page per frame...
 */
typedef struct folio {
    uint32_t frame_page;
    uint32_t refcount;
} frame_t;

/* Memory-efficient doubly linked list of frames
 * REDUNDANT
 * As all frame objects will live in effectively an array, we only need
 * to be able to index into that array.
 */
typedef struct {
    /* Index of first element in list */
    frame_t* first;
    /* Index in last element of list */
    frame_t* last;
    /* Size of list (useful for debugging) */
    uint64_t length;
} frame_list_t;

#define FOLIO_COUNT (BUFFERS_SIZE + 1)
/* Bytes of pager_memory frame_table_init() needs for the folio metadata. */
#define FOLIO_MEMORY_SIZE (FOLIO_COUNT * sizeof(struct folio))

/**
 * Takes over memory (FOLIO_MEMORY_SIZE bytes) for folio metadata, retypes the
 * global zero page and fills the free lists. frame_cnode is where frame caps
 * are placed, gzp_cnode where the copies of the global zero page cap go.
 */
void frame_table_init(uintptr_t memory, seL4_CPtr frame_cnode, seL4_CPtr gzp_cnode);

struct folio *get_folio_from_idx(uint32_t idx);

/**
 * Takes a frame off the unused list with a refcount of one.
 */
struct folio *get_frame();
/**
 * Drops a reference to a frame and returns it to the unused list.
 */
void put_frame(struct folio *folio);

/**
 * Takes a copy of the global zero page cap off the unused list.
 */
uint32_t get_gzp();
void put_gzp(uint32_t gzp);

/* CSpace addresses of a frame and of a global zero page cap copy. */
seL4_CPtr frame_cptr(uint32_t frame);
seL4_CPtr gzp_cptr(uint32_t gzp);

#endif
