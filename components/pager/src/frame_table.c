/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "frame_table.h"
#include "cspace.h"
#include "page_table.h"
#include "pager.h"
#include "untyped.h"

#include <string.h>
#include <sddf/util/printf.h>

seL4_CPtr frame_cnode_cptr;
seL4_CPtr zero_page_cnode_cptr;
seL4_CPtr frame_copy_cnode_cptr;

/* Somewhere in our own address space clear of every MR we were given, used to
 * get at a frame's contents. */
#define SCRATCH_VADDR 0x30000000

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

/* Copies still carrying a mapping we have since overwritten. */
static uint32_t dirty_gzp[BUFFERS_SIZE];
static uint32_t dirty_gzp_idx = 0;

struct folio *get_folio_from_idx(uint32_t idx) {
    if (idx == 0 || idx > FOLIO_COUNT) {
        sddf_printf("folio index out of range: %u\n", idx);
        return NULL;
    }
    return (struct folio *)(frame_memory + (idx - 1) * sizeof(struct folio)); 
}

// Slub allocator local functions.
static void refill_frames(uint32_t n) {
    sddf_dprintf("refilling frames\n");
    while (n) {
        uint32_t batch = n > RETYPE_BATCH ? RETYPE_BATCH : n;
        if (frame_idx + batch > FOLIO_COUNT || unused_frames_idx + batch > BUFFERS_SIZE) {
            sddf_printf("frame metadata exhausted\n");
            return;
        }
        seL4_Error err = untyped_alloc(seL4_ARM_SmallPageObject, seL4_PageBits, frame_idx, frame_cnode_cptr, batch);
        if (err) {
            sddf_printf("error occured when refilling frames %d\n", err);
            return;
        }
        for (uint32_t i = 0; i < batch; ++i) {
            struct folio *folio = get_folio_from_idx(frame_idx);
            folio->frame_page = frame_idx;
            folio->refcount = 0;
            unused_frames[unused_frames_idx] = folio;
            ++frame_idx;
            ++unused_frames_idx;
        }
        n -= batch;
    }
}

struct folio *get_frame() {
    if (!unused_frames_idx) {
        refill_frames(REFILL_SIZE);
        if (!unused_frames_idx) {
            return NULL;
        }
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

static void refill_gzp(uint32_t n) {
    sddf_dprintf("refilling gzp\n");
    // there is no batched seL4_CNode_Copy, one mapping needs one cap.
    while (n--) {
        if (unused_gzp_idx >= BUFFERS_SIZE) {
            return;
        }
        seL4_Error err = seL4_CNode_Copy(zero_page_cnode_cptr, gzp_idx, 58, frame_cnode_cptr, global_zero_page, 58, create_cap_rights(false));
        if (err) {
            sddf_printf("error occured when copying GZP caps %d\n", err);
            return;
        }
        unused_gzp[unused_gzp_idx] = gzp_idx;
        ++gzp_idx;
        ++unused_gzp_idx;
    }
}

/* Unmapping is what makes a dirty copy reusable; the PTE it used to own has
 * already been overwritten, so the kernel does no TLB work here. */
static void drain_gzp(uint32_t n) {
    while (n-- && dirty_gzp_idx && unused_gzp_idx < BUFFERS_SIZE) {
        uint32_t gzp = dirty_gzp[--dirty_gzp_idx];
        seL4_Error err = seL4_ARM_Page_Unmap(gzp_cptr(gzp));
        if (err) {
            sddf_printf("error occured when unmapping a GZP cap %d\n", err);
            continue;
        }
        unused_gzp[unused_gzp_idx++] = gzp;
    }
}

uint32_t get_gzp() {
    if (!unused_gzp_idx) {
        drain_gzp(REFILL_SIZE);
        if (!unused_gzp_idx) {
            refill_gzp(REFILL_SIZE);
            if (!unused_gzp_idx) {
                return 0;
            }
        }
    }
    return unused_gzp[--unused_gzp_idx];
}

void put_gzp(uint32_t gzp) {
    if (unused_gzp_idx < BUFFERS_SIZE) {
        unused_gzp[unused_gzp_idx++] = gzp;
    }
}

void put_dirty_gzp(uint32_t gzp) {
    if (dirty_gzp_idx < BUFFERS_SIZE) {
        dirty_gzp[dirty_gzp_idx++] = gzp;
    }
}

void frame_table_zero_gzp(seL4_CPtr vspace)
{
    // one call per missing level, seL4 maps at whichever it finds first.
    for (int i = 0; i < 3; ++i) {
        seL4_ARM_PageTable_Map(ips_cptr(get_ips()), vspace, SCRATCH_VADDR, seL4_ARM_Default_VMAttributes);
    }
    seL4_Error err = seL4_ARM_Page_Map(frame_cptr(global_zero_page), vspace, SCRATCH_VADDR,
                                       create_cap_rights(true), seL4_ARM_Default_VMAttributes);
    if (err) {
        sddf_printf("error occured on mapping the global zero page %d\n", err);
        return;
    }
    memset((void *)SCRATCH_VADDR, 0, PAGE_SIZE);
    err = seL4_ARM_Page_Unmap(frame_cptr(global_zero_page));
    if (err) {
        sddf_printf("error occured on unmapping the global zero page %d\n", err);
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
    untyped_alloc(seL4_ARM_SmallPageObject, seL4_PageBits, frame_idx, frame_cnode_cptr, 1);
    ++frame_idx;
    sddf_printf("global zero page is %d\n", global_zero_page);

    // refill buffers
    refill_frames(INIT_FRAMES);
    // copy a bunch of zero pages to the zero page thing
    refill_gzp(INIT_GZP);
}
