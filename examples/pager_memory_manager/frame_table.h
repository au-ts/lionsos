#ifndef FRAME_TABLE_H
#define FRAME_TABLE_H

#include "types.h"
#include <microkit.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <os/sddf.h>
#include <sddf/blk/queue.h> 
#include <sddf/blk/storage_info.h>
#include <sddf/blk/config.h>
#include <sddf/util/printf.h>

unsigned long long time = 0; // Working set clock.

/**
 * wsclock hand ptr.
 */
FrameInfo *wshand[MAX_PDS] = {NULL};

FrameInfo frame_table[MAX_PDS][NUM_PT_ENTRIES]; // this functions as a doubly ll.

static inline void move_hand(uint32_t pd_idx) {
    wshand[pd_idx] = &frame_table[pd_idx][wshand[pd_idx]->next];
}

/**
 * Used to calculate the offset of the frame to get the address of the frame within the pager.
 */
uintptr_t get_frame_offset(uintptr_t frame_addr, int pd_idx) {
    return (frame_addr - (uintptr_t)frame_table[pd_idx]) / sizeof(FrameInfo);
}

/**
 * Gets the next frame to allocate, may need to page out the frame
 * currently recursive, may/may not want to change.
 */
static FrameInfo *get_frame(uint32_t pd_idx) {
    if (!wshand[pd_idx]->page) {
        FrameInfo *ret = wshand[pd_idx];
        move_hand(pd_idx);
        return ret;
    }

    if (wshand[pd_idx]->page->recently_used) {
        wshand[pd_idx]->page->recently_used = false;
        microkit_arm_page_unmap(wshand[pd_idx]->cap);
        move_hand(pd_idx);
    } else {
        FrameInfo *ret = wshand[pd_idx];
        move_hand(pd_idx);
        return ret;
    }

    return get_frame(pd_idx);
}

#endif