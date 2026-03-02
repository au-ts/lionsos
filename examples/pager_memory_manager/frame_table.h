#include "types.h"
#include <stdint.h>
#include <stddef.h>

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
 * Gets the next frame to allocate, may need to page out the frame
 * currently recursive, may/may not want to change.
 */
static FrameInfo *get_frame(uint32_t pd_idx) {
    if (!wshand[pd_idx]->page) {
        FrameInfo *ret = wshand[pd_idx];
        move_hand(pd_idx);
        return ret;
    }

    if (wshand[pd_idx]->page->dirty) {
        wshand[pd_idx]->page->dirty = false;
        move_hand(pd_idx);
    } else if (time - wshand[pd_idx]->last_accessed < TAU)
    {
        move_hand(pd_idx); // this has potential to cause infinite loop if I don't increment time.
    } else {
        FrameInfo *ret = wshand[pd_idx];
        move_hand(pd_idx);
        return ret;
    }

    return get_frame(pd_idx);
}