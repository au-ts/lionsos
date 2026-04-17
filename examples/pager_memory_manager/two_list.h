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

static void remove_from_list(struct list *l, tl_frame_t *node);
static void push_head(struct list *l, tl_frame_t *node);
void promote_page(tl_frame_t *frame); // Added prototype for forward reference
static void refill_inactive(int pd_idx, int num);

static struct list activelist[MAX_PDS];
static struct list inactivelist[MAX_PDS];
static struct list freelist[MAX_PDS];
tl_frame_t frame_table[MAX_PDS][NUM_PT_ENTRIES]; // this is a static list of frames
int free_frames_idx[MAX_PDS] = {-1};

/**
 * initialise the free frames
 */
inline void init_frame(frame_pd_id *current_frame) {
    int pd_idx = current_frame->pd_idx;
    ++free_frames_idx[pd_idx];
    frame_table[pd_idx][free_frames_idx[pd_idx]] = (tl_frame_t){ .cap = current_frame->frame_cap, .page = NULL, .next = NULL, .prev = NULL, .active = false, .dirty = false };
    push_head(&freelist[pd_idx], &frame_table[pd_idx][free_frames_idx[pd_idx]]);
}

/**
 * Used to calculate the offset of the frame to get the address of the frame within the pager.
 */
uintptr_t get_frame_offset(uintptr_t frame_addr, int pd_idx) {
    return (frame_addr - (uintptr_t)frame_table[pd_idx]) / sizeof(tl_frame_t);
}

void push_head_active(tl_frame_t *frame) {
    if (frame->active) {
        remove_from_list(&activelist[frame->pd_idx], frame);
        push_head(&activelist[frame->pd_idx], frame);
    } else {
        promote_page(frame);
    }
}

void promote_page(tl_frame_t *frame) {
    remove_from_list(&inactivelist[frame->pd_idx], frame);
    push_head(&activelist[frame->pd_idx], frame);
    frame->active = true;
}

/**
 * returns the prev frame.
 */
tl_frame_t *demote_page(tl_frame_t *frame) {
    tl_frame_t *prev = frame->prev;
    remove_from_list(&activelist[frame->pd_idx], frame);
    push_head(&inactivelist[frame->pd_idx], frame);
    frame->active = false;
    return prev;
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

static void refill_inactive(int pd_idx, int num) {
    sddf_printf("refilling inactive\n");
    while (num) {
        tl_frame_t *ptr = activelist[pd_idx].tail;
        if (ptr == NULL) return; // prevent infinite loops.
        while (ptr != NULL) {
            if (ptr->referenced) {
                // move to head
                ptr->referenced = false;
                microkit_arm_page_unmap(ptr->cap);
                remove_from_list(&activelist[pd_idx], ptr);
                push_head(&activelist[pd_idx], ptr);
            } else {
                // move to inactive
                ptr = demote_page(ptr);
                if (!num--) return;
            }
        }
    }
}


// this is only called during eviction, so frame should be cleared.
tl_frame_t *get_frame(uint32_t pd_idx) {
    // If we have free frames, use one
    if (freelist[pd_idx].head != NULL) {
        tl_frame_t *frame = freelist[pd_idx].head;
        remove_from_list(&freelist[pd_idx], frame);
        push_head(&activelist[pd_idx], frame);
        frame->active = true;
        return frame;
    }

    // Otherwise, evict from inactive/active
    if (inactivelist[pd_idx].size < 2) {
        refill_inactive(pd_idx, NUM_PT_ENTRIES / 2);
    }

    tl_frame_t *ef = inactivelist[pd_idx].tail;
    promote_page(ef);
    return ef;
}

/**
 * Frees a frame: removes it from the active/inactive list 
 * and adds it back to the free frames pool.
 */
void free_frame(tl_frame_t *frame) {
    if (frame == NULL) return;
    
    // 1. Unmap hardware
    microkit_arm_page_unmap(frame->cap);
    
    // 2. Remove from active/inactive list
    if (frame->active) {
        remove_from_list(&activelist[frame->pd_idx], frame);
    } else {
        remove_from_list(&inactivelist[frame->pd_idx], frame);
    }

    // 3. Reset metadata
    frame->page = NULL;
    frame->active = false;
    frame->dirty = false;
    frame->referenced = false;
    
    // 4. Add to free list
    push_head(&freelist[frame->pd_idx], frame);
}

#endif