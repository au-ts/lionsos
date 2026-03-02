#include <microkit.h>
#include <stdint.h>
#include "types.h"
#include "pagefile.h"
#include "frame_table.h"

/** Assumptions and Restrictions:
 * - there is a fixed maximum of PD's (64 currently)
 * - Heap is bounded to 128 4k frames.
 */


/** Questions:
 * - How do I set the dirtybit???
 * - How do I do concurrency?
 * - How do I write to the file?
 */

static uint64_t unmapped_frames_addr;
static uint64_t num_frames;

// TODO: figure out where I need to outline this...
__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;
static blk_queue_handle_t blk_queue;



frame_pd_id *frames = (frame_pd_id *) unmapped_frames_addr;

// so that we don't process the same fault multiple times.
uintptr_t current_faults[MAX_PDS] = {-1};




// stuff required for the vm fault handling
// I cannot have actual page tables due to missing malloc.
pe page_table[MAX_PDS][NUM_PT_ENTRIES]; // page tables for the children. // each pd has 512 total max pages in heap.






// TODO: have process vspace ptrs here as well.
unsigned long vspaces[MAX_PDS];


static inline pe retrieve_page(uintptr_t fault_addr, uint32_t pd_idx) {
    return page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
}

void init(void)
{

    // initialise and check blk queue and config
    assert(blk_config_check_magic(&blk_config));
    LOG_CLIENT("config check\n");
    blk_queue_init(&blk_queue, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr,
                   blk_config.virt.num_buffers);
    LOG_CLIENT("queue init\n");

    // initialise the frame caps.
    int frame_indicies[MAX_PDS] = {0};
    for (int i = 0; i < num_frames; ++i) {
        uint32_t next = i + 1, prev = i - 1;
        frame_pd_id *cur_frame = &frames[i];
        int pd_idx = cur_frame->pd_idx;

        frame_table[pd_idx][frame_indicies[pd_idx]] = { .cap = cur_frame->frame_cap, .last_accessed = 0, page = NULL, .next = ++frame_indicies[pd_idx]};
    }

    
    for (int i = 0; i < num_frames; ++i) {
        // set the wshand to the start for every pd
        wshand[i] = frame_table[i];
        // make the frame tables circular
        frame_table[i][frame_indicies[i] - 1].next = 0; 
    }
}

void after_page_in(uint32_t pd_idx, uintptr_t fault_addr) {
    // map the page to the frame
    microkit_arm_page_map(frame->cap, vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
    frame->page = page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
    current_faults[pd_idx] = -1;
}

void page_in(uint32_t pd_idx, uintptr_t fault_addr) {

}

void after_page_out(uint32_t pd_idx, uintptr_t fault_addr) {
    // check whether or not I need to page in or not.
    if (page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)].frame_addr) {
        page_in(pd_idx, fault_addr);
    } else {
        after_page_in(pd_idx, fault_addr);
    }
}

void page_out(uint32_t pd_idx, uintptr_t fault_addr) {
    // find empty slot in pagefile
    int slot = get_pagefile_slot();
    // mark in page entry where the pagefile entry is.
    page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)].pagefile_offset = slot;
    // queue the write with page after_page_out();

}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    ++time;
    // TODO: this is when the child has a vm fault...
    uintptr_t fault_addr = microkit_mr_get(1); // I am not sure if this is the right mr number so will need to check later.
    uint32_t pd_idx = microkit_mr_get(0);

    // check that the fault is not currently being served
    if (current_faults[pd_idx] == ROUND_DOWN_TO_4K(fault_addr)) {
        return; // i need to return the bool
    } else {
        current_faults[pd_idx] = ROUND_DOWN_TO_4K(fault_addr);
    }

    FrameInfo *frame = get_frame(pd_idx);

    // frame has a associated page, therefore we need to page out.
    if (frame->page) {
        page_out();
    } else {
        after_page_out();
    }
}

/**
 * This gets called once the write/read operation is complete.
 */
void notified(microkit_channel ch)
{
    // TODO: this may not be required 
}

// NOT USED BELOW:
seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    // TODO: this may not be required.
}



