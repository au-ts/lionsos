/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cspace.h"
#include "pager.h"
#include "page_table.h"

#define PAGE_BITS 12ULL
#define PAGE_SIZE 4096ULL
#define PAGE_MASK 0xFFFFFFFFFFFFF000ULL
#define ROUND_DOWN_TO_4K(x)      ((uintptr_t)(x) & PAGE_MASK)
#define seL4_PageBits           12

#define MAPPING_SLOTS 3u
#define UNTYPED_SLOT 1 // this is the cptr thing basically.
#define FRAME_CNODE 2 // where frame caps are placed
#define IPS_CNODE 3 // where intermediary paging structure caps are placed.
#define GZP_CNODE 4 // where global zero frame caps are placed.
#define BUFFERS_SIZE 200000
#define REFILL_SIZE 20000

#include <sddf/benchmark/config.h>
#include <sddf/benchmark/bench.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".benchmark_client_config"))) benchmark_client_config_t benchmark_config;


// capability related global variables:
uintptr_t remaining_untypeds_vaddr;
static cnode_specs_t post_boot_cnode;
capDLBootInfo_t *capDLBootInfo;
// uint64_t untyped_idx;
uint32_t vspaces[10]; // child_idx to vspace_idx
static seL4_CPtr frame_cnode_cptr;
static seL4_CPtr ips_cnode_cptr;
static seL4_CPtr gzp_cnode_cptr;
static uint32_t global_zero_page;



// Below is essentially a slub allocator.
/** pager_memory static allocation for pager to use. */
pgd_t page_tables[10];
uintptr_t pager_memory;
static uintptr_t pager_memory_idx;

// Bookkeeping for pager's memory.
static uintptr_t freed_pager_memory[BUFFERS_SIZE];
static uint32_t freed_pager_memory_idx = 0;


// Bookkeeping for intermediary paging structures
static uint32_t unused_ips[BUFFERS_SIZE];
static uint32_t unused_ips_idx = 0;

static uint32_t ips_idx = 1;

// Bookkeeping for frames.
static uint32_t unused_frames[BUFFERS_SIZE];
static uint32_t unused_frames_idx = 0;

static uint32_t frame_idx = 1;

// Bookkeeping for global zero pages.
static uint32_t gzp_idx = 1;

static uint32_t unused_gzp[BUFFERS_SIZE];
static uint32_t unused_gzp_idx = 0;

// Slub allocator local functions.
static void refill_frames() {
    sddf_dprintf("refilling frames\n");
    for (int i = 0; i < REFILL_SIZE * 10; ++i) {
        seL4_Error err = do_untyped_retype(&post_boot_cnode, seL4_ARM_SmallPageObject, seL4_PageBits, frame_idx, frame_cnode_cptr);
        if (err) {
            sddf_printf("error occured when refilling frames %d\n", err);
        }
        unused_frames[unused_frames_idx] = frame_idx;
        ++frame_idx;
        ++unused_frames_idx;
    }
}

static uint32_t get_frame() {
    if (!unused_frames_idx) {
        refill_frames();
    }
    return unused_frames[--unused_frames_idx];
}

static void refill_ips() {
    sddf_dprintf("refilling ips\n");
    for (int i = 0; i < REFILL_SIZE; ++i) {
        seL4_Error err = do_untyped_retype(&post_boot_cnode, seL4_ARM_PageTableObject, 12, ips_idx, ips_cnode_cptr);
        if (err) {
            sddf_printf("error occured when creating ips caps %d\n", err);
        }
        unused_ips[unused_ips_idx] = ips_idx;
        ++ips_idx;
        ++unused_ips_idx;
    }
}
static uint32_t get_ips() {
    if (!unused_ips_idx) {
        refill_ips();
    }
    return unused_ips[--unused_ips_idx];
}

static void refill_gzp() {
    sddf_dprintf("refilling gzp\n");
    for (int i = 0; i < REFILL_SIZE; ++i) {
        seL4_Error err = seL4_CNode_Copy(gzp_cnode_cptr, gzp_idx, 58, frame_cnode_cptr, global_zero_page, 58, create_cap_rights(false));
        if (err) {
            sddf_printf("error occured when copying GZP caps %d\n", err);
        }
        unused_gzp[unused_gzp_idx] = gzp_idx;
        ++gzp_idx;
        ++unused_gzp_idx;
    }
}
static uint32_t get_gzp() {
    if (!unused_gzp_idx) {
        refill_gzp();
    }
    return unused_gzp[--unused_gzp_idx];
}

uintptr_t allocate_page_table() {
    
    if (freed_pager_memory_idx) {
        return freed_pager_memory[--freed_pager_memory_idx];
    }
    uint32_t size = sizeof(struct pud); // they are all the same size.
    uintptr_t ret = pager_memory + pager_memory_idx;
    pager_memory_idx += size;
    return ret;
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
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cnode_cptr + pud->cap, vspace_idx, vaddr, seL4_ARM_Default_VMAttributes);
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
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cnode_cptr + pd->cap, vspace_idx, vaddr, seL4_ARM_Default_VMAttributes);
        if (err) {
            sddf_dprintf("error when mapping page tables pd%d\n", err);
        }
    }
    pt_t *pt = pd->entries[PT_INDEX(vaddr)];
    if (!pt) {
        // allocat pt;
        pt = (pt_t *) allocate_page_table();
        pd->entries[PT_INDEX(vaddr)] = pt;
        pt->cap = get_ips();
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cnode_cptr + pt->cap, vspace_idx, vaddr, seL4_ARM_Default_VMAttributes);
        if (err) {
            sddf_dprintf("error when mapping page tables pt %d\n", err);
        }
    }
    return &pt->entries[PAGE_INDEX(vaddr)];
}


void init(void)
{
    pager_memory_idx = 0;

    // // intialise untypeds.
    capDLBootInfo = (capDLBootInfo_t*) remaining_untypeds_vaddr;
    ips_cnode_cptr = microkit_cspace_root_slot_to_cptr(3);
    frame_cnode_cptr = microkit_cspace_root_slot_to_cptr(2);
    gzp_cnode_cptr = microkit_cspace_root_slot_to_cptr(4);
    post_boot_cnode.cptr = microkit_cspace_root_slot_to_cptr(1);
    post_boot_cnode.start = capDLBootInfo->untypeds.start;
    // // TODO: is end empty?
    for (uint64_t i = capDLBootInfo->untypeds.start; i < capDLBootInfo->untypeds.end; i++) {
        post_boot_cnode.caps[i].base_addr = capDLBootInfo->untypedList[i].paddr;
        post_boot_cnode.caps[i].end_addr = post_boot_cnode.caps[i].base_addr + (1ULL << capDLBootInfo->untypedList[i].sizeBits);
        post_boot_cnode.caps[i].is_device = capDLBootInfo->untypedList[i].isDevice;
        post_boot_cnode.caps[i].object_type = seL4_UntypedObject;
        post_boot_cnode.end = i + 1;
        sddf_dprintf("i: %lu, 0x%lx-0x%lx: device? %d\n", i, post_boot_cnode.caps[i].base_addr, post_boot_cnode.caps[i].end_addr, post_boot_cnode.caps[i].is_device);
    }
    update_active_ut_idx(&post_boot_cnode);
    sddf_dprintf("cnode start: %d\n", post_boot_cnode.start);

    // create the global zero page.
    global_zero_page = frame_idx;
    do_untyped_retype(&post_boot_cnode, seL4_ARM_SmallPageObject, seL4_PageBits, frame_idx, frame_cnode_cptr);
    ++frame_idx;
    // refill unused at the start
    sddf_printf("global zero page is %d\n", global_zero_page);
    // copy a bunch of zero pages to the zero page thing

    // refill buffers
    refill_frames();
    refill_gzp();
    refill_ips();
    // sddf_dprintf("%d\n", sizeof(pgd_t));
    // while (1);
}

void notified(microkit_channel ch)
{
    // does nothing for now.
}

/**
 * Create intermediary paging structures.
 * Map page.
 * Return.
 * 
 * cspace - this is the difficult part... idk how to do that... 
 * vspace
 */
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // microkit_pd_stop(child);
    // get fault info
    uintptr_t fault_addr = ROUND_DOWN_TO_4K(microkit_mr_get(1));
    uint64_t fsr = microkit_mr_get(3);
    uintptr_t ip = microkit_mr_get(0);
    uint64_t fsc = fsr & 0x3F;
    bool is_write = (fsr >> 6) & 1;
    uint32_t frame;
    pte_t *page_entry = make_page_table_entry(fault_addr, child);

    // // TODO: implement access flag faults.
    // if (fsc >= 0x08 && fsc <= 0x0B) {
    //     // Access flag fault (level 0–3)
    //     // unset access flag
    // }
    // Translation fault (level 0–3)
    if (fsc >= 0x04 && fsc <= 0x07) {
        // if it is a read fault, map global zero page.
        if (!is_write) {
            frame = get_gzp();
            seL4_Error err = err = seL4_ARM_Page_Map(gzp_cnode_cptr + frame, vspaces[child], fault_addr, create_cap_rights(is_write), 0x03);
            if (err) {
                sddf_printf("error occured on map frame zero %d\n", err);
            }
            // add global zero page to the frame cap.
            insert_frame_to_page(frame, page_entry);
            return seL4_True;
        } else {
            frame = get_frame();
            *page_entry |= DESC_NG;
        }
        insert_frame_to_page(frame, page_entry);
    }
    // Permission fault (level 1–3)
    if (fsc >= 0x0D && fsc <= 0x0F) {
        // if global zero page get new frame
        if (!(*page_entry & DESC_NG)) {
            frame = get_frame();
            insert_frame_to_page(frame, page_entry);
            *page_entry |= DESC_NG;
        } else {
            frame = get_frame_from_page(*page_entry);
            insert_frame_to_page(frame, page_entry);
        }
    }

    // do mapping
    seL4_Error err = seL4_ARM_Page_Map(frame_cnode_cptr + frame, vspaces[child], fault_addr, create_cap_rights(is_write), 0x03);
    if (err) {
        sddf_printf("error occured on map frame %d\n", err);
    }
    return seL4_True;
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    bool cmd = microkit_mr_get(0);
    if (cmd == 1) {
        sddf_notify(benchmark_config.start_ch);
    } else if (cmd == 0) {
        sddf_notify(benchmark_config.stop_ch);
    }
    return microkit_msginfo_new(0, 0);
}

/**
 * TODO: do the actual implementation
 */
seL4_CapRights_t create_cap_rights(bool is_write) {
    return seL4_CapRights_new(1, is_write, 1, 1);
}

static seL4_Error map_frame(uint64_t frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                                 seL4_CapRights_t rights, seL4_ARM_VMAttributes attr, int num) {
    /* Attempt the mapping */
    seL4_Error err;
    for (int i = 0; i < num; ++i) {
        uint32_t page_table_cap = get_ips();
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cnode_cptr + page_table_cap, vspace, vaddr, seL4_ARM_Default_VMAttributes);
        if (err != seL4_NoError) sddf_printf("mapping failed! frame cap is %d, vspace is %d vaddr is %p err is %d\n", frame_cap, vspace, vaddr, err);
    }
    err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
    return err;
}

/**
 * TODO: implement.
 */
void myfree(uintptr_t start, uintptr_t end, microkit_child child) {
    // Unmaps the page
    // Unmap paging structures
    // return page and paging structures to their free lists.
    // free shadow page tables.
    // zero out frames.
    // add freed stuff to free lists.
}
