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

seL4_CPtr paging_cnode_cptr;

/** Shadow page table roots, one per child. */
static pgd_t page_tables[PAGER_MAX_CLIENTS];

/**
 * Below is essentially a slub allocator.
 * pager_memory static allocation for the shadow page tables to use.
 */
static uintptr_t table_memory;
static uintptr_t table_memory_idx;
static uintptr_t table_memory_size;

// Bookkeeping for the pager's memory.
static uintptr_t freed_tables[BUFFERS_SIZE];
static uint32_t freed_tables_idx = 0;

// Bookkeeping for intermediary paging structures
static uint32_t unused_ips[BUFFERS_SIZE];
static uint32_t unused_ips_idx = 0;

static uint32_t ips_idx = 1;

static void refill_ips(uint32_t n) {
    sddf_dprintf("refilling ips\n");
    while (n) {
        uint32_t batch = n > RETYPE_BATCH ? RETYPE_BATCH : n;
        if (unused_ips_idx + batch > BUFFERS_SIZE) {
            sddf_printf("ips buffer full\n");
            return;
        }
        seL4_Error err = untyped_alloc(seL4_ARM_PageTableObject, 12, ips_idx, paging_cnode_cptr, batch);
        if (err) {
            sddf_printf("error occured when creating ips caps %d\n", err);
            return;
        }
        for (uint32_t i = 0; i < batch; ++i) {
            unused_ips[unused_ips_idx++] = ips_idx++;
        }
        n -= batch;
    }
}

uint32_t get_ips() {
    if (!unused_ips_idx) {
        refill_ips(REFILL_SIZE);
        if (!unused_ips_idx) {
            return 0;
        }
    }
    return unused_ips[--unused_ips_idx];
}

void put_ips(uint32_t ips) {
    if (unused_ips_idx < BUFFERS_SIZE) {
        unused_ips[unused_ips_idx++] = ips;
    }
}

/* The arena is zeroed once in page_table_init and tables are zeroed as they are
 * freed, so there is nothing to clear here. */
static uintptr_t allocate_page_table() {
    
    if (freed_tables_idx) {
        return freed_tables[--freed_tables_idx];
    }
    uint32_t size = sizeof(struct pud); // they are all the same size.
    if (table_memory_idx + size > table_memory_size) {
        sddf_printf("out of shadow page table memory\n");
        return 0;
    }
    uintptr_t ret = table_memory + table_memory_idx;
    table_memory_idx += size;
    return ret;
}

static void free_page_table(uintptr_t table) {
    if (freed_tables_idx < BUFFERS_SIZE) {
        memset((void *)table, 0, sizeof(struct pud));
        freed_tables[freed_tables_idx++] = table;
    }
}

pgd_t *page_table_root(uint32_t child) {
    return &page_tables[child];
}

static uint32_t map_paging_structure(seL4_CPtr vspace, uintptr_t vaddr) {
    uint32_t ips = get_ips();
    seL4_Error err = seL4_ARM_PageTable_Map(ips_cptr(ips), vspace, vaddr, seL4_ARM_Default_VMAttributes);
    if (err == seL4_DeleteFirst) {
        put_ips(ips);
        return 0;
    }
    if (err) {
        sddf_dprintf("error when mapping a paging structure at 0x%lx %d\n", vaddr, err);
    }
    return ips;
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
        if (!pud) return NULL;
        vspace->entries[PUD_INDEX(vaddr)] = pud;
        pud->cap = map_paging_structure(vspace_idx, vaddr);
    }
    pd_t *pd = pud->entries[PD_INDEX(vaddr)];
    if (!pd) {
        // allocate pd; & pt
        pd = (pd_t *) allocate_page_table();
        if (!pd) return NULL;
        pud->entries[PD_INDEX(vaddr)] = pd;
        pd->cap = map_paging_structure(vspace_idx, vaddr);
    }
    pt_t *pt = pd->entries[PT_INDEX(vaddr)];
    if (!pt) {
        // allocat pt;
        pt = (pt_t *) allocate_page_table();
        if (!pt) return NULL;
        pd->entries[PT_INDEX(vaddr)] = pt;
        pt->cap = map_paging_structure(vspace_idx, vaddr);
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

static void unmap_paging_structure(uint32_t ips) {
    if (!ips) {
        return;
    }
    seL4_Error err = seL4_ARM_PageTable_Unmap(ips_cptr(ips));
    if (err != seL4_NoError) {
        sddf_dprintf("error unmapping paging structure %u: %d\n", ips, err);
    }
    put_ips(ips);
}

void unmap_range(uintptr_t start, uintptr_t end, uint32_t child) {
    if (child >= PAGER_MAX_CLIENTS || start >= end) {
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
            uint32_t copy = get_copy_from_page(*entry);
            bool is_private = (*entry & DESC_NG) != 0;

            seL4_Error err = seL4_NoError;
            if (is_private && copy) {
                // Deleting the copy unmaps it.
                delete_copied_frame_cap(copy);
            } else {
                err = seL4_ARM_Page_Unmap(mapping_cptr(*entry));
                if (err != seL4_NoError) {
                    sddf_dprintf("error unmapping frame %u: %d\n", frame, err);
                }
            }

            if (is_private) {
                struct folio *folio = get_folio_from_idx(frame);
                if (err == seL4_NoError && folio && folio->refcount <= 1) {

                    // TODO: need to figure out a way to zero out frames more efficiently.
                    // frame_table_zero_frame(frame);
                }
                put_frame(folio);
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
        unmap_paging_structure(pt->cap);
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
        unmap_paging_structure(pd->cap);
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
        unmap_paging_structure(pud->cap);
        free_page_table((uintptr_t)pud);
        vspace->entries[PUD_INDEX(vaddr)] = NULL;
    }
}

void page_table_init(uintptr_t memory, uint64_t size, seL4_CPtr paging_cnode)
{
    table_memory = memory;
    table_memory_idx = 0;
    table_memory_size = size;
    paging_cnode_cptr = paging_cnode;
    /* seL4 does not zero what it retypes, so do it once here rather than per
     * table in allocate_page_table(). */
    memset((void *)table_memory, 0, size);

    refill_ips(INIT_IPS);
}
