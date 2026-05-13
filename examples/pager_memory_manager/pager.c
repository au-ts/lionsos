#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include "types.h"
#include "pagefile.h"
#include "two_list.h"
#include <os/sddf.h>
#include <sddf/blk/queue.h> 
#include <sddf/blk/storage_info.h>
#include <sddf/blk/config.h>
#include <sddf/util/printf.h>
#include <string.h>
#include "pager.h"

// #define FRAME_DATA 0x8000000000

#define HEAP_SIZE 128 * 4096

uint64_t unmapped_frames_addr;
uint64_t num_frames;
uint64_t thing;

// TODO: figure out where I need to outline this...
// maybe: blk_system.add_client(pager, partition=partition)
__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;
static blk_queue_handle_t blk_queue;



frame_pd_id *frames;

// so that we don't process the same fault multiple times.
struct fault_info current_faults[MAX_PDS];

// this is where the heaps are all located.
char *heaps = (char *)FRAME_DATA;



// stuff required for the vm fault handling
// I cannot have actual page tables due to missing malloc.
pe page_table[MAX_PDS][NUM_PT_ENTRIES]; // page tables for the children. // each pd has 512 total max pages in heap.




void memset0(void* begin, int num) {
    char *ptr = (char *)begin;
    for (int i = 0; i < num; ++i) {
        ptr[i] = 0;
    }
}

// TODO: have process vspace ptrs here as well.
unsigned int vspaces[MAX_PDS];


static inline pe *retrieve_page(uintptr_t fault_addr, uint32_t pd_idx) {
    if (fault_addr > MMAP_START) {
        if (INDEX_INTO_MMAP_ARRAY(fault_addr) - 134217128 < 300) sddf_printf("somehow the thing is less than 300\n");
        return &page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr) - 134217128];
    }
    if (INDEX_INTO_MMAP_ARRAY(fault_addr) > 299) sddf_printf("somethow thte thing is greater than 299 %d\n", INDEX_INTO_MMAP_ARRAY(fault_addr));
    return &page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
}

void init(void)
{
    sddf_printf("%d many frames\n", num_frames);
    // initialise the page table
    memset0(page_table, MAX_PDS * NUM_PT_ENTRIES * sizeof(pe));
    // i need to set the pagefile slot as -1 i think.
    for (int i = 0; i < MAX_PDS; ++i) {
        for (int j = 0; j < NUM_PT_ENTRIES; ++j) {
            page_table[i][j].pagefile_offset = -1;
        }
        free_frames_idx[i] = -1;
        current_faults[i] = (struct fault_info){ .write = false, .pc = -1};
    } 
    

    // initialise and check blk queue and config
    frames = (frame_pd_id *) unmapped_frames_addr;
    // should be an assert.
    blk_config_check_magic(&blk_config);
    // LOG_CLIENT("config check\n");
    blk_queue_init(&blk_queue, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr,
                   blk_config.virt.num_buffers);
    // LOG_CLIENT("queue init\n");

    // initialises the frame caps.
    int frame_indicies[MAX_PDS] = {0};
    for (int i = 0; i < num_frames; ++i) {
        uint32_t next = i + 1, prev = i - 1;
        frame_pd_id *cur_frame = &frames[i];
        init_frame(cur_frame);
    }
    frame_map_addr = (uintptr_t)frames;
}

/**
 * The vm fault may occur for one of three reasons:
 * - RO perms, need to set the dirtybit/used
 * - setting the reference bit.
 * - Actual VM fault
 * - THE PROBLEM IS THAT THE PD INDEX IS DIFFERENT FROM THE CHILD NUM...
 */
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{   
    microkit_pd_stop(child);
    // TODO: make sure that i map as rw and with a dirty bit if the fault is a write fault.
    uintptr_t fault_addr = ROUND_DOWN_TO_4K(microkit_mr_get(1));
    uint64_t fsr = microkit_mr_get(2);
    bool is_write = (fsr >> 1) & 1;
    uint32_t pd_idx = child;
    // uint64_t program_counter = microkit_mr_get(0);
    pe *page = retrieve_page(fault_addr, pd_idx);
    tl_frame_t *old_frame = page->frame_addr;
    current_faults[child].pc = microkit_mr_get(0);
    current_faults[child].write = is_write;
    if (old_frame) {
        old_frame->referenced = true;
        if (is_write) old_frame->dirty = true;
        
        int err = microkit_arm_page_map_rw(old_frame->cap, vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
        if (err) sddf_printf("there is an error\n");
        if (err) return seL4_True;
        // sddf_printf("before returning true\n");
        microkit_pd_restart(child, current_faults[child].pc);
        return seL4_True;
    }

    tl_frame_t *frame = get_frame(pd_idx);
    if (frame->dirty) {
        // do a page out
        page_out(frame, pd_idx, fault_addr);
        return seL4_True;
    } else if (frame->page) {
        if (frame->page->pagefile_offset == -1) {
            // gotta do a page out because there is no pagefile for this frame.
            page_out(frame, pd_idx, fault_addr);
            return seL4_True;
        } else {
            // not dirty so can just do a unmap (old pagefile slot is good enough)
            microkit_arm_page_unmap(frame->cap);
            frame->page->frame_addr = NULL;
        }
        
    }
    after_page_out(frame, pd_idx, fault_addr);
    return seL4_True;
}

/**
 * This gets called once the write/read operation is complete.
 */
void notified(microkit_channel ch)
{
    // assert(iich == blk_config.virt.id); // sanity check, pager should only be notified by this.
    
    blk_resp_status_t status = -1;
    uint16_t count = -1;
    uint32_t id = -1;

    // int err = blk_dequeue_resp(&blk_queue, &status, &count, &id);
    while (blk_dequeue_resp(&blk_queue, &status, &count, &id) != -1) {
        // assert(!err);
        // assert(status == BLK_RESP_OK);
        // assert(count == 1); // make sure that the write/read is actually done.
        // TODO: if necessary make a thing to recover from the error.

        // queue the next thing depending on what was done.
        if (page_continuations[id].state == PAGE_OUT) {
            // unmap the frame.
            microkit_arm_page_unmap(page_continuations[id].frame->cap);
            after_page_out( page_continuations[id].frame, page_continuations[id].pd_idx, page_continuations[id].fault_addr);
        } else {
            after_page_in(page_continuations[id].frame, page_continuations[id].pd_idx, page_continuations[id].fault_addr, true);
        }
    }
}



void after_page_in(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr, bool paged_in) {
    // map the page to the frame
    if (paged_in) {
        memcpy(get_frame_data(frame),
       (char *)blk_config.data.vaddr, 4096);
    }
    int err = 0;
    if (current_faults[pd_idx].write) {
        err = microkit_arm_page_map_rw(frame->cap, vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
        frame->dirty = true;
    } else {
        err = microkit_arm_page_map_ro(frame->cap, vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
        frame->dirty = false;
    }
    if (err) {
        sddf_printf("page map failed! %d\n", err);
    }
    frame->page = retrieve_page(fault_addr, pd_idx);
    retrieve_page(fault_addr, pd_idx)->frame_addr = frame;
    frame->pd_idx = pd_idx;
    // sddf_printf("sending from reply cap\n");
    // sddf_printf("before returning true\n");
    if (frame->page->pagefile_offset != -1) mark_pagefile_slot_free(frame->page);
    microkit_pd_restart(pd_idx, current_faults[pd_idx].pc);
    // microkit_send(4);
}

void page_in(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr) {
    // sddf_printf("paging in %p\n", fault_addr);
    // get the slot
    int slot = retrieve_page(fault_addr, pd_idx)->pagefile_offset;
    // queue the read
    int request_id = get_request_id();
    page_continuations[request_id] = (struct page_request_info){ .frame = frame, .pd_idx = pd_idx, .fault_addr = fault_addr, .state = PAGE_IN }; // TODO: fill this out with relevant info.
    blk_enqueue_req(&blk_queue, BLK_REQ_READ, 0, slot, 1, request_id);
    sddf_notify(blk_config.virt.id);
}

void after_page_out(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr) {
    // check whether or not I need to page in or not.
    if (retrieve_page(fault_addr, pd_idx)->pagefile_offset != -1) {
        page_in(frame, pd_idx, fault_addr);
    } else {
        after_page_in(frame, pd_idx, fault_addr, false);
    }
}

void page_out(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr) {
    // find empty slot in pagefile
    int slot = get_pagefile_slot();
    // mark in page entry where the pagefile entry is.
    pe* page = frame->page;
    page->pagefile_offset = slot;
    // queue the write with page after_page_out();
    int request_id = get_request_id();
    page_continuations[request_id] = (struct page_request_info){ .frame = frame, .pd_idx = pd_idx, .fault_addr = fault_addr, .state = PAGE_OUT }; // TODO: fill this out with relevant info.
    char *data_dest = (char *) blk_config.data.vaddr;
    memcpy(data_dest, get_frame_data(frame), 4096);
    blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, 0, slot, 1, request_id);
    sddf_notify(blk_config.virt.id);

    // make sure to do the metadata stuff
    // pe *page = &retrieve_page(fault_addr, pd_idx)
    // page->frame_addr = NULL;
    page->frame_addr = NULL;
}

/**
 * This is when a free is called.
 */
seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    int pd_idx = microkit_mr_get(0);
    uintptr_t addr = microkit_mr_get(1);

    pe *page = retrieve_page(addr, pd_idx);
    tl_frame_t *frame = page->frame_addr;
    page->frame_addr = NULL;
    if (page->pagefile_offset != -1) mark_pagefile_slot_free(page);

    // i also need to add this frame to the free list.
    free_frame(frame);
}



