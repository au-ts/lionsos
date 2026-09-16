/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "frame_table.h"
#include "cspace.h"
#include "pager.h"
#include "untyped.h"

#include <sddf/util/printf.h>

static seL4_CPtr frame_cnode_cptr;
static seL4_CPtr zero_page_cnode_cptr;
static seL4_CPtr frame_copy_cnode_cptr;

/* Folio metadata, bump-allocated out of the memory given to frame_table_init(). */
static uintptr_t frame_memory;
static uint32_t frame_idx = 1;

/* The frame every read-only anonymous mapping is backed by. */
static uint32_t global_zero_page;

// Bookkeeping for frames.
static struct folio *unused_frames[BUFFERS_SIZE];
static uint32_t unused_frames_idx = 0;

// Bookkeeping for global zero pages.
static uint32_t gzp_idx = 1;

static uint32_t unused_gzp[BUFFERS_SIZE];
static uint32_t unused_gzp_idx = 0;

struct folio *get_folio_from_idx(uint32_t idx) {
    if (idx == 0 || idx > FOLIO_COUNT) {
        sddf_printf("folio index out of range: %u\n", idx);
        return NULL;
    }
    return (struct folio *)(frame_memory + (idx - 1) * sizeof(struct folio)); 
}

seL4_CPtr frame_cptr(uint32_t frame) {
    return frame_cnode_cptr + frame;
}

seL4_CPtr gzp_cptr(uint32_t gzp) {
    return zero_page_cnode_cptr + gzp;
}

seL4_CPtr frame_copy_cptr(uint32_t copy) {
    return frame_copy_cnode_cptr + copy;
}

// Slub allocator local functions.
static void refill_frames() {
    sddf_dprintf("refilling frames\n");
    for (int i = 0; i < REFILL_SIZE * 10; ++i) {
        struct folio *folio = get_folio_from_idx(frame_idx);
        if (folio == NULL || unused_frames_idx >= BUFFERS_SIZE) {
            sddf_printf("frame metadata exhausted\n");
            return;
        }
        seL4_Error err = untyped_alloc(seL4_ARM_SmallPageObject, seL4_PageBits, frame_idx, frame_cnode_cptr);
        if (err) {
            sddf_printf("error occured when refilling frames %d\n", err);
        }
        folio->frame_page = frame_idx;
        folio->refcount = 0;
        unused_frames[unused_frames_idx] = folio;
        ++frame_idx;
        ++unused_frames_idx;
    }
}

struct folio *get_frame() {
    if (!unused_frames_idx) {
        refill_frames();
    }
    struct folio *folio = unused_frames[--unused_frames_idx];
    folio->refcount = 1;
    return folio;
}

void put_frame(struct folio *folio) {
    if (folio == NULL) {
        return;
    }
    if (folio->refcount > 0) {
        --folio->refcount;
    }
    if (unused_frames_idx < BUFFERS_SIZE) {
        unused_frames[unused_frames_idx++] = folio;
    }
}

static void refill_gzp() {
    sddf_dprintf("refilling gzp\n");
    for (int i = 0; i < REFILL_SIZE; ++i) {
        seL4_Error err = seL4_CNode_Copy(zero_page_cnode_cptr, gzp_idx, 58, frame_cnode_cptr, global_zero_page, 58, create_cap_rights(false));
        if (err) {
            sddf_printf("error occured when copying GZP caps %d\n", err);
        }
        unused_gzp[unused_gzp_idx] = gzp_idx;
        ++gzp_idx;
        ++unused_gzp_idx;
    }
}

uint32_t get_gzp() {
    if (!unused_gzp_idx) {
        refill_gzp();
    }
    return unused_gzp[--unused_gzp_idx];
}

void put_gzp(uint32_t gzp) {
    if (unused_gzp_idx < BUFFERS_SIZE) {
        unused_gzp[unused_gzp_idx++] = gzp;
    }
}

void frame_table_init(uintptr_t memory, seL4_CPtr frame_cnode,
                      seL4_CPtr zero_page_cnode, seL4_CPtr frame_copy_cnode)
{
    frame_memory = memory;
    frame_cnode_cptr = frame_cnode;
    zero_page_cnode_cptr = zero_page_cnode;
    frame_copy_cnode_cptr = frame_copy_cnode;

    // create the global zero page.
    global_zero_page = frame_idx;
    untyped_alloc(seL4_ARM_SmallPageObject, seL4_PageBits, frame_idx, frame_cnode_cptr);
    ++frame_idx;
    sddf_printf("global zero page is %d\n", global_zero_page);

    // refill buffers
    refill_frames();
    // copy a bunch of zero pages to the zero page thing
    refill_gzp();
}
