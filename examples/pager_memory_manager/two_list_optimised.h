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

static struct list activelist[MAX_PDS];
static struct list inactivelist[MAX_PDS];

tl_frame_t frame_table[MAX_PDS][NUM_PT_ENTRIES]; // this is a static list of frames
int free_frames_idx[MAX_PDS] = {-1};

/**
 * initialise the free frames
 */
inline void init_frame(frame_pd_id *current_frame) {
    int pd_idx = current_frame->pd_idx;
    ++free_frames_idx[pd_idx];
    frame_table[pd_idx][free_frames_idx[pd_idx]] = (tl_frame_t){ .cap = current_frame->frame_cap, .page = NULL, .next = NULL, .prev = NULL };
}

/**
 * Used to calculate the offset of the frame to get the address of the frame within the pager.
 */
uintptr_t get_frame_offset(uintptr_t frame_addr, int pd_idx) {
    return (frame_addr - (uintptr_t)frame_table[pd_idx]) / sizeof(tl_frame_t);
}

void promote_page(tl_frame_t *frame) {
    remove_from_list(&inactivelist[frame->pd_idx], frame);
    push_head(&activelist[frame->pd_idx], frame);
}

static void remove_from_list(struct list *l, tl_frame_t *node) {
    if (l->size == 1) {
        l->head = NULL;
        l->tail = NULL;
    } else if (node == l->head) {
        l->head = node->next;
        l->head->prev = NULL;
    } else if (node == l->tail) {
        l->tail = node->prev;
        l->tail->next = NULL;
    } else {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }
    node->next = NULL;
    node->prev = NULL;
    l->size--;
}

static void push_head(struct list *l, tl_frame_t *node) {
    node->prev = NULL;
    node->next = l->head;
    if (l->head != NULL) {
        l->head->prev = node;
    } else {
        l->tail = node;
    }
    l->head = node;
    l->size++;
}

static void rebalance_lists(int pd_idx) {
    tl_frame_t *ptr = activelist[pd_idx].head;
    struct list *al = &activelist[pd_idx];
    struct list *il = &inactivelist[pd_idx];

    while (ptr != NULL) {
        tl_frame_t *nptr = ptr->next;
        if (ptr->referenced) {
            ptr->referenced = false;
            microkit_arm_page_unmap(ptr->cap);
        } else {
            remove_from_list(al, ptr);
            push_head(il, ptr);
        }
        ptr = nptr;
    }
}

tl_frame_t *evict_frame(uint32_t pd_idx) {
    if (free_frames_idx[pd_idx] != -1) {
        tl_frame_t *frame = &frame_table[pd_idx][free_frames_idx[pd_idx]];
        --free_frames_idx[pd_idx];
        push_head(&activelist[pd_idx], frame);
        return frame;
    }

    if (inactivelist[pd_idx].size < 2) {
        rebalance_lists(pd_idx);
    }

    tl_frame_t *ef = inactivelist[pd_idx].tail;
    remove_from_list(&inactivelist[pd_idx], ef);
    push_head(&activelist[pd_idx], ef);
    return ef;
}

#endif