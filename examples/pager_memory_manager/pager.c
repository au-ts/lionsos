#include <microkit.h>
#include <stdint.h>
#include "types.h"
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






frame_pd_id *frames = (frame_pd_id *) unmapped_frames_addr;





// stuff required for the vm fault handling
// I cannot have actual page tables due to missing malloc.
pe page_table[MAX_PDS][NUM_PT_ENTRIES]; // page tables for the children. // each pd has 512 total max pages in heap.


FrameInfo frame_table[MAX_PDS][NUM_PT_ENTRIES]; // this functions as a doubly ll.

/**
 * wsclock hand ptr.
 */
FrameInfo *wshand[MAX_PDS] = {NULL};

// TODO: have process vspace ptrs here as well.
unsigned long vspaces[MAX_PDS];
unsigned long long time = 0; // Working set clock.


static inline void move_hand(uint32_t pd_idx) {
    wshand[pd_idx] = &frame_table[pd_idx][wshand[pd_idx]->next];
}

/**
 * Gets the next frame to allocate, may need to page out the frame
 * currently recursive, may/may not want to change.
 */
static FrameInfo *get_frame(uint32_t pd_idx) {
    if (!wshand[pd_idx]->page) {
        FrameInfo *ret = wshand;
        move_hand(pd_idx);
        return ret;
    }

    if (wshand[pd_idx]->page->dirty) {
        --wshand[pd_idx]->page->dirty;
        move_hand(pd_idx);
    } else if (time - wshand[pd_idx]->last_accessed < TAU)
    {
        move_hand(pd_idx); // this has potential to cause infinite loop if I don't increment time.
    } else {
        FrameInfo *ret = wshand;
        move_hand(pd_idx);
        return ret;
    }

    return get_frame(pd_idx);
}

static inline pe retrieve_page(uintptr_t fault_addr, uint32_t pd_idx) {
    return page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
}

void init(void)
{
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

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    ++time;
    // TODO: this is when the child has a vm fault...
    uintptr_t fault_addr = microkit_mr_get(1); // I am not sure if this is the right mr number so will need to check later.
    uint32_t pd_idx = microkit_mr_get(0);
    // check if a page in is required.
    FrameInfo *frame = get_frame(pd_idx);
    if (frame->page) {
        // page out.
    }

    if (page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)].frame_addr) {
        // page in.
    }

    // map the page to the frame
    microkit_arm_page_map(frame->cap, vspaces[pd_idx], ROUND_DOWN_TO_4K(fault_addr));
    frame->page = page_table[pd_idx][INDEX_INTO_MMAP_ARRAY(fault_addr)];
}


// NOT USED BELOW:

void notified(microkit_channel ch)
{
    // TODO: this may not be required 
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    // TODO: this may not be required.
}



