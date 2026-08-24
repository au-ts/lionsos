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
#define FRAME_CNODE 2
#define IPS_CNODE 3
#define GZP_CNODE 4
#define BUFFERS_SIZE 20000

#include <sddf/benchmark/config.h>
#include <sddf/benchmark/bench.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".benchmark_client_config"))) benchmark_client_config_t benchmark_config;

uintptr_t remaining_untypeds_vaddr;

static cnode_specs_t post_boot_cnode;
capDLBootInfo_t *capDLBootInfo;
// uint64_t untyped_idx;
uint32_t vspaces[10]; // child_idx to vspace_idx





/** pager_memory static allocation for pager to use. */
uintptr_t pager_memory;
static uintptr_t pager_memory_idx;

uintptr_t freed_pager_memory[BUFFERS_SIZE];
uint32_t freed_pager_memory_idx = 0;

uint32_t unused_frames[BUFFERS_SIZE];
uint32_t unused_frames_idx = 0;

uint32_t frame_list[BUFFERS_SIZE];
uint32_t frame_list_idx = 0;

uint32_t unused_paging_structures[BUFFERS_SIZE];
uint32_t unused_paging_structures_idx = 0;

uint32_t paging_structure_list[BUFFERS_SIZE];
uint32_t paging_structure_list_idx = 0;


uintptr_t frame_buffer;






static frame_list_t unused_frame_list;

static seL4_CPtr frame_cnode_cptr;
static seL4_CPtr ips_cnode_cptr;
static seL4_CPtr gzp_cnode_cptr;

static uint32_t global_zero_page;

// a four level page table for each child.
static pt_t page_tables[10][512];

static uint32_t frame_idx = 1;
static uint32_t ips_idx = 1;

// TODO proper allocation of GZP pointers
static uint32_t gzp_idx = 0;
static uint32_t gzp_offset = 1;


seL4_Error delete_global_zero_frame_cap(uint32_t cap) {
    return seL4_CNode_Delete(cap + frame_cnode_cptr, 0, 0);
} 

void create_zero_caps(int num) {
    for (int i = 0; i < num; ++i) {
        ++gzp_idx;
        int err = seL4_CNode_Copy(gzp_cnode_cptr, gzp_idx, 58, frame_cnode_cptr, global_zero_page, 58, create_cap_rights(false));
        if (err != seL4_NoError) {
            sddf_printf("copy fail %d iteration %d,,,, destination %lx source %lx\n", err, i, gzp_cnode_cptr + gzp_idx, frame_cnode_cptr + global_zero_page);
            while(1);
        }
    }
}

void init(void)
{
    // initialise the frame lists
    unused_frame_list.first = NULL;
    unused_frame_list.last = NULL;
    unused_frame_list.length = 0;
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
    do_untyped_retype(&post_boot_cnode, seL4_ARM_SmallPageObject, seL4_PageBits, &frame_idx, frame_cnode_cptr);
    get_frame_from_idx(global_zero_page)->frame_page = global_zero_page;
    // refill unused at the start
    refill_unused();
    sddf_printf("global zero page is %d\n", global_zero_page);
    // copy a bunch of zero pages to the zero page thing
    create_zero_caps(20000);
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
    frame_t *frame;
    int num;
    uint64_t *page_entry = get_page_table_entry(fault_addr, page_tables[child], &num);

    // TODO: implement access flag faults.
    if (fsc >= 0x08 && fsc <= 0x0B) {
        // Access flag fault (level 0–3)
        // unset access flag
    }
    // Translation fault (level 0–3)
    if (fsc >= 0x04 && fsc <= 0x07) {
        // if it is a read fault, map global zero page.
        if (!is_write) {
            seL4_Error err = map_frame(gzp_cnode_cptr + gzp_offset, vspaces[child], fault_addr, create_cap_rights(is_write), 0x03);
            ++gzp_offset;
            if (err) {
                sddf_printf("error occured on map frame zero %d\n", err);
            }
            return seL4_True;
        } else {
            frame = get_unused_frame();
            *page_entry |= DESC_NG;
        }
        insert_frame_to_page(frame->frame_page, page_entry);
    }
    // Permission fault (level 1–3)
    if (fsc >= 0x0D && fsc <= 0x0F) {
        // if global zero page get new frame
        if (!(*page_entry & DESC_NG) || !*page_entry) {
            frame = get_unused_frame();
            insert_frame_to_page(frame->frame_page, page_entry);
        } else {
            frame = get_frame_from_idx(get_frame_from_page(*page_entry));
        }
    }

    // do mapping
    seL4_Error err = map_frame(frame_cnode_cptr + frame->frame_page, vspaces[child], fault_addr, create_cap_rights(is_write), 0x03);
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
        seL4_Error err = retype_map_pt(vspace, vaddr);
        if (err != seL4_NoError) sddf_printf("mapping failed! frame cap is %d, vspace is %d vaddr is %p err is %d\n", frame_cap, vspace, vaddr, err);
    }
    err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
    return err;
}

frame_t *get_frame_from_idx(uint32_t frame_idx) {
    return (frame_t *) (frame_buffer + (sizeof(frame_t) * (frame_idx - 1)));
}

/**
 * Refills unused_frame_list using untyped memory
 * 10 new frames.
 * TODO:
 * - page reclaim if necessary.
 * - could move to frame_table.c
 */
void refill_unused() {
    // TODO: check if there is more memory left.
    // assert(capDLBootInfo->untypeds.start + untyped_idx + 10 <= capDLBootInfo->untypeds.end);
    // assumes the untyped_idx does not overflow.
    
    // untyped_idx is the frame cap essentially.
    // get 10 frame_t's from the pager memory.
    
    for (int i = 0; i < 20000; ++i) {
        // create frame_t and move to end of list.
        frame_t *new_folio = get_frame_from_idx(frame_idx);
        new_folio->next = NULL;
        new_folio->frame_page = frame_idx;
        int err = do_untyped_retype(&post_boot_cnode, seL4_ARM_SmallPageObject, seL4_PageBits, &frame_idx, frame_cnode_cptr);
        if (unused_frame_list.length) {
            unused_frame_list.last->next = new_folio;
            unused_frame_list.last = new_folio;
        } else {
            unused_frame_list.first = new_folio;
            unused_frame_list.last = new_folio;
        }
        ++unused_frame_list.length;
    }
}

/**
 * Moves unused frame to used frame list 
 */
frame_t *get_unused_frame() {
    if (!unused_frame_list.length) {
        refill_unused();
    } 
    // get the last element and remove it
    frame_t *ret = unused_frame_list.first;
    if (unused_frame_list.length == 1) {
        unused_frame_list.last = NULL;
        unused_frame_list.first = NULL;
    } else {
        unused_frame_list.first = ret->next;
    }
    --unused_frame_list.length;
    
    return ret;
}

seL4_Error retype_map_pt(seL4_CPtr vspace, seL4_Word vaddr) {
    // retype untyped into the pt object
    uint32_t ips_slot = ips_idx;
    int err = do_untyped_retype(&post_boot_cnode, seL4_ARM_PageTableObject, 12, &ips_idx, ips_cnode_cptr);
    if (err != seL4_NoError) {
        sddf_printf("retyping to page table failed %d\n", err);
    } 
    return seL4_ARM_PageTable_Map(ips_cnode_cptr + ips_slot, vspace, vaddr, seL4_ARM_Default_VMAttributes);
}

// TODO: track allocations for frees.
uintptr_t allocate_pager_memory(uint64_t size) {
    if (freed_pager_memory_idx) {
        --freed_frame_memory_idx;
        return freed_pager_memory[freed_pager_memory_idx];
    }
    uintptr_t ret = pager_memory + pager_memory_idx;
    pager_memory_idx += size;
    return ret;
}

/**
 * TODO: implement.
 */
void free(uintptr_t start, uintptr_t end) {
    // Unmaps the page
    // Unmap paging structures
    // return page and paging structures to their free lists.
    // free shadow page tables.
}
