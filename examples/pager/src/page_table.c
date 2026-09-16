/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "page_table.h"
#include "frame_table.h"
#include "pager.h"
#include "untyped.h"

#include <stdbool.h>
#include <string.h>
#include <sddf/util/printf.h>

static seL4_CPtr ips_cnode_cptr;

/** Shadow page table roots, one per child. */
static pgd_t page_tables[MAX_CHILDREN];

/**
 * Below is essentially a slub allocator.
 * pager_memory static allocation for the shadow page tables to use.
 */
static uintptr_t table_memory;
static uintptr_t table_memory_idx;

// Bookkeeping for the pager's memory.
static uintptr_t freed_tables[BUFFERS_SIZE];
static uint32_t freed_tables_idx = 0;

// Bookkeeping for intermediary paging structures
static uint32_t unused_ips[BUFFERS_SIZE];
static uint32_t unused_ips_idx = 0;

static uint32_t ips_idx = 1;

// 
void insert_frame_to_page(uint32_t const frame, uint64_t* page) {
    *page |= ((uint64_t) frame) << 12;
}

// get bits 12:47
uint32_t get_frame_from_page(uint64_t const page) {
    return (page >> 12) & 0xFFFFFFFFFULL;
}

seL4_CPtr ips_cptr(uint32_t ips) {
    return ips_cnode_cptr + ips;
}

static void refill_ips() {
    sddf_dprintf("refilling ips\n");
    for (int i = 0; i < REFILL_SIZE; ++i) {
        seL4_Error err = untyped_alloc(seL4_ARM_PageTableObject, 12, ips_idx, ips_cnode_cptr);
        if (err) {
            sddf_printf("error occured when creating ips caps %d\n", err);
        }
        unused_ips[unused_ips_idx] = ips_idx;
        ++ips_idx;
        ++unused_ips_idx;
    }
}

uint32_t get_ips() {
    if (!unused_ips_idx) {
        refill_ips();
    }
    return unused_ips[--unused_ips_idx];
}

void put_ips(uint32_t ips) {
    if (unused_ips_idx < BUFFERS_SIZE) {
        unused_ips[unused_ips_idx++] = ips;
    }
}

static uintptr_t allocate_page_table() {
    
    if (freed_tables_idx) {
        return freed_tables[--freed_tables_idx];
    }
    uint32_t size = sizeof(struct pud); // they are all the same size.
    uintptr_t ret = table_memory + table_memory_idx;
    table_memory_idx += size;
    memset((void *)ret, 0, size);
    return ret;
}

static void free_page_table(uintptr_t table) {
    if (freed_tables_idx < BUFFERS_SIZE) {
        freed_tables[freed_tables_idx++] = table;
    }
}

pgd_t *page_table_root(uint32_t child) {
    return &page_tables[child];
}

/**
 * make shadow page table entry
 * creates mappings for intermediary paging structures
 */
pte_t *make_page_table_entry(uintptr_t vaddr, uint32_t child) {
    pgd_t *vspace = &page_tables[child];
    uint32_t vspace_idx = vspaces[child];
    pud_t *pud = vspace->entries[PUD_INDEX(vaddr)];
    if (!pud) {
        // allocate pud; & pt
        pud = (pud_t *) allocate_page_table();
        vspace->entries[PUD_INDEX(vaddr)] = pud;
        pud->cap = get_ips();
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cptr(pud->cap), vspace_idx, vaddr, seL4_ARM_Default_VMAttributes);
        if (err) {
            sddf_dprintf("error when mapping page tables pud %d\n", err);
        }
    }
    pd_t *pd = pud->entries[PD_INDEX(vaddr)];
    if (!pd) {
        // allocate pd; & pt
        pd = (pd_t *) allocate_page_table();
        pud->entries[PD_INDEX(vaddr)] = pd;
        pd->cap = get_ips();
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cptr(pd->cap), vspace_idx, vaddr, seL4_ARM_Default_VMAttributes);
        if (err) {
            sddf_dprintf("error when mapping page tables pd %d\n", err);
        }
    }
    pt_t *pt = pd->entries[PT_INDEX(vaddr)];
    if (!pt) {
        // allocat pt;
        pt = (pt_t *) allocate_page_table();
        pd->entries[PT_INDEX(vaddr)] = pt;
        pt->cap = get_ips();
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cptr(pt->cap), vspace_idx, vaddr, seL4_ARM_Default_VMAttributes);
        if (err) {
            sddf_dprintf("error when mapping page tables pt %d\n", err);
        }
    }
    return &pt->entries[PAGE_INDEX(vaddr)];
}

seL4_Error map_frame(uint64_t frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr, int num) {
    /* Attempt the mapping */
    seL4_Error err;
    for (int i = 0; i < num; ++i) {
        uint32_t page_table_cap = get_ips();
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cptr(page_table_cap), vspace, vaddr, seL4_ARM_Default_VMAttributes);
        if (err != seL4_NoError) sddf_printf("mapping failed! frame cap is %d, vspace is %d vaddr is %p err is %d\n", frame_cap, vspace, vaddr, err);
    }
    err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
    return err;
}

void unmap_range(uintptr_t start, uintptr_t end, uint32_t child) {
    if (child >= MAX_CHILDREN || start >= end) {
        return;
    }

    start = ROUND_DOWN_TO_4K(start);
    end = ROUND_UP_TO_4K(end);

    for (uintptr_t vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
        pgd_t *vspace = &page_tables[child];
        pud_t *pud = vspace->entries[PUD_INDEX(vaddr)];
        if (!pud) {
            continue;
        }
        pd_t *pd = pud->entries[PD_INDEX(vaddr)];
        if (!pd) {
            continue;
        }
        pt_t *pt = pd->entries[PT_INDEX(vaddr)];
        if (!pt) {
            continue;
        }

        pte_t *entry = &pt->entries[PAGE_INDEX(vaddr)];
        if (*entry) {
            uint32_t frame = get_frame_from_page(*entry);
            seL4_CPtr frame_cptr_to_unmap = (*entry & DESC_NG)
                ? frame_cptr(frame)
                : gzp_cptr(frame);
            seL4_Error err = seL4_ARM_Page_Unmap(frame_cptr_to_unmap);
            if (err != seL4_NoError) {
                sddf_dprintf("error unmapping frame %u: %d\n", frame, err);
            }

            if (*entry & DESC_NG) {
                put_frame(get_folio_from_idx(frame));
            } else {
                put_gzp(frame);
            }
            *entry = 0;
        }

        bool pt_empty = true;
        for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
            if (pt->entries[i]) {
                pt_empty = false;
                break;
            }
        }
        if (!pt_empty) {
            continue;
        }
        seL4_Error err = seL4_ARM_PageTable_Unmap(ips_cptr(pt->cap));
        if (err != seL4_NoError) {
            sddf_dprintf("error unmapping pt %u: %d\n", pt->cap, err);
        }
        put_ips(pt->cap);
        free_page_table((uintptr_t)pt);
        pd->entries[PT_INDEX(vaddr)] = NULL;

        bool pd_empty = true;
        for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
            if (pd->entries[i]) {
                pd_empty = false;
                break;
            }
        }
        if (!pd_empty) {
            continue;
        }
        err = seL4_ARM_PageTable_Unmap(ips_cptr(pd->cap));
        if (err != seL4_NoError) {
            sddf_dprintf("error unmapping pd %u: %d\n", pd->cap, err);
        }
        put_ips(pd->cap);
        free_page_table((uintptr_t)pd);
        pud->entries[PD_INDEX(vaddr)] = NULL;

        bool pud_empty = true;
        for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
            if (pud->entries[i]) {
                pud_empty = false;
                break;
            }
        }
        if (!pud_empty) {
            continue;
        }
        err = seL4_ARM_PageTable_Unmap(ips_cptr(pud->cap));
        if (err != seL4_NoError) {
            sddf_dprintf("error unmapping pud %u: %d\n", pud->cap, err);
        }
        put_ips(pud->cap);
        free_page_table((uintptr_t)pud);
        vspace->entries[PUD_INDEX(vaddr)] = NULL;
    }
}

void page_table_init(uintptr_t memory, seL4_CPtr ips_cnode)
{
    table_memory = memory;
    table_memory_idx = 0;
    ips_cnode_cptr = ips_cnode;

    refill_ips();
}
