#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include "types.h"
#include "pagefile.h"
#include "frame_table.h"
#include <os/sddf.h>
#include <sddf/blk/queue.h> 
#include <sddf/blk/storage_info.h>
#include <sddf/blk/config.h>
#include <sddf/util/printf.h>
#include <string.h>

// i need to find out what the pd idxs are.

/**
 * TODOS:
 * - I need to map the heap frames to the pager in the .system file.
 * 
 * I think I will probably eat a lot of disk and memory with this design...
 */

 /**
  * REFACTOR TODOS:
  * - Do error checking for blk functions.
  * - Static & const functions and variables
  * - inline the required functions.
  * 
  */

/** Assumptions and Restrictions:
 * - there is a fixed maximum of PD's (64 currently)
 * - Heap is bounded to 128 4k frames.
 * - Child PD heaps must be mapped to pager.
 */

/**
 * This is where the heaps should begin, it is important that the pd's heaps are in order of pd_idx and contiguous.
 * The size of the heap should always be the same HEAP_SIZE = 524288 bytes.
 */
#define FRAME_DATA 0x8000000000

#define PD_IDX_OFFSET 2 // this is to index into the frame data, 2 because pager and memory manager will be the first 2 indicies.
#define HEAP_SIZE 128 * 4096

uint64_t unmapped_frames_addr;
uint64_t num_frames;
uint32_t pager_vspace;
uint64_t thing;

// TODO: figure out where I need to outline this...
// maybe: blk_system.add_client(pager, partition=partition)
__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;
static blk_queue_handle_t blk_queue;



frame_pd_id *frames;

// so that we don't process the same fault multiple times.
uintptr_t current_faults[MAX_PDS] = {-1};

// this is where the heaps are all located.
char *heaps = (char *)FRAME_DATA;

char *get_frame_data(int pd_idx, uintptr_t frame_offset) {
    return heaps + ((pd_idx - PD_IDX_OFFSET) * HEAP_SIZE + (frame_offset * 4096));
}

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


static inline pe retrieve_page(uintptr_t fault_addr, uint32_t pd_idx) {
    return page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
}

void init(void)
{
    // // TODO: memset stuff to 0 where required.
    memset0(page_table, MAX_PDS * NUM_PT_ENTRIES * sizeof(pe));

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
        int pd_idx = cur_frame->pd_idx;
        frame_table[pd_idx][frame_indicies[pd_idx]] = (FrameInfo){ .cap = cur_frame->frame_cap, .last_accessed = 0, .page = NULL, .next = ++frame_indicies[pd_idx] };
        // sddf_printf("pd_idx = %d\n", pd_idx);
    }

    // setup wshand and make the frame tables circular.
    for (int i = 0; i < MAX_PDS; ++i) {
        if (frame_indicies[i]) {
            wshand[i] = frame_table[i];
            frame_table[i][frame_indicies[i] - 1].next = 0;
        }
        
    }
    for (int i = 0; i < 10; ++i) {
        sddf_printf("vspace at id %d is %d\n", i, vspaces[i]);
    }
    
}

void after_page_in(FrameInfo *frame, uint32_t pd_idx, uintptr_t fault_addr) {
    // map the page to the frame
    sddf_printf("after page in cap %d\n", frame->cap);
    microkit_arm_page_map_ro(frame->cap, vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
    frame->page = &page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
    page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)].frame_addr = frame;
    current_faults[pd_idx] = -1;
    frame->pd_idx = pd_idx;
}

void page_in(FrameInfo *frame, uint32_t pd_idx, uintptr_t fault_addr) {
    sddf_printf("paging in\n");
    // get the slot
    int slot = page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)].pagefile_offset;
    // queue the read
    int request_id = get_request_id();
    page_continuations[request_id] = (struct page_request_info){ .frame = frame, .pd_idx = pd_idx, .fault_addr = fault_addr, .state = PAGE_IN }; // TODO: fill this out with relevant info.
    memcpy(get_frame_data(pd_idx, get_frame_offset((uintptr_t)&frame_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)], pd_idx)), (char *)blk_config.data.vaddr, 4096);
    blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, 0, slot, 1, request_id);
    sddf_notify(blk_config.virt.id);
}

void after_page_out(FrameInfo *frame, uint32_t pd_idx, uintptr_t fault_addr) {
    // check whether or not I need to page in or not.
    if (page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)].frame_addr) {
        page_in(frame, pd_idx, fault_addr);
    } else {
        after_page_in(frame, pd_idx, fault_addr);
    }
}

void page_out(FrameInfo *frame, uint32_t pd_idx, uintptr_t fault_addr) {
    // find empty slot in pagefile
    int slot = get_pagefile_slot();
    // mark in page entry where the pagefile entry is.
    frame->page->pagefile_offset = slot;
    // queue the write with page after_page_out();
    int request_id = get_request_id();
    if (frame->page->dirty) {
        page_continuations[request_id] = (struct page_request_info){ .frame = frame, .pd_idx = pd_idx, .fault_addr = fault_addr, .state = PAGE_OUT }; // TODO: fill this out with relevant info.
        char *data_dest = (char *) blk_config.data.vaddr;
        memcpy(data_dest, get_frame_data(frame->pd_idx, get_frame_offset((uintptr_t)frame, frame->pd_idx)), 4096);
        blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, 0, slot, 1, request_id);
        sddf_notify(blk_config.virt.id);
    } else {
        microkit_arm_page_unmap(frame->cap);
        after_page_out(frame, pd_idx, fault_addr);
    }
    
}

/**
 * The vm fault may occur for one of two reasons:
 * - RO perms, need to set the dirtybit/used
 * - Actual VM fault
 * - THE PROBLEM IS THAT THE PD INDEX IS DIFFERENT FROM THE CHILD NUM...
 */
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    child += 3; // this is necessary as the child num should be same as pd index or everything just  falls apart.
    ++time;
    // TODO: this is when the child has a vm fault...
    uintptr_t fault_addr = microkit_mr_get(1); // I am not sure if this is the right mr number so will need to check later.
    uint32_t pd_idx = child;
    // sddf_dprintf("fault with child %d\n", child);
    // check if I just need to remap with write perms.
    // sddf_dprintf("fault with mmap array index %d\n", INDEX_INTO_MMAP_ARRAY(fault_addr));
    FrameInfo *old_frame = page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)].frame_addr; // old frame is null or garbage...
    pe *page = &page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
    // if (old_frame) sddf_printf("old frame is %p and page is %p\n", page, old_frame->page);
    if (old_frame && old_frame->page == page) {
        // int err = microkit_arm_page_unmap(old_frame->cap); // I don't know if I actually need to unmap the frame. maybe this is an unnecessary step...
        // sddf_printf("err num is %d\n", err);
        int err = microkit_arm_page_map_rw(old_frame->cap, vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
        sddf_printf("err num is %d, addr is %p, vspace is %d\n", err, fault_addr, vspaces[pd_idx]);
        // i need to mark the page as dirty and recently used
        page->recently_used = true;
        page->dirty = true;
        // sddf_printf("only had to remap was rw for addr %p\n", fault_addr);
        return seL4_True;
    }

    // check that the fault is not currently being served
    if (current_faults[pd_idx] == ROUND_DOWN_TO_4K(fault_addr)) {
        return seL4_True;
    } else {
        current_faults[pd_idx] = ROUND_DOWN_TO_4K(fault_addr);
    }

    FrameInfo *frame = get_frame(pd_idx);

    // frame has a associated page, therefore we need to page out.
    if (frame->page) { 
        // TODO: The page out should page out the frame's page, not the faulting page...
        sddf_printf("paging out\n");
        page_out(frame, pd_idx, fault_addr);
    } else {
        after_page_out(frame, pd_idx, fault_addr);
    }
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
    blk_dequeue_resp(&blk_queue, &status, &count, &id);
    sddf_printf("pager notified with status %d\n", page_continuations[id].state);
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
        after_page_in(page_continuations[id].frame, page_continuations[id].pd_idx, page_continuations[id].fault_addr);
    }

}

// NOT USED BELOW:
seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    // TODO: this may not be required.
}



