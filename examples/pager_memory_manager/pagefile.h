#ifndef PAGEFILE_H
#define PAGEFILE_H

static int pagefile_size = 0;
static int pagefile_freed_slots_stack[4000];
static int pagefile_stack_ptr = 0;

static inline int get_pagefile_slot() {
    if (pagefile_stack_ptr) {
        return pagefile_freed_slots_stack[--pagefile_stack_ptr];
    }
    return ++pagefile_size;
}

static inline void mark_pagefile_slot_free(int idx) {
    pagefile_freed_slots_stack[pagefile_stack_ptr++] = idx;
}

// this is to store info to continue the things.
struct page_request_info page_continuations[10];
static int request_id = 0;
int get_request_id() {
    if (request_id == 9) {
        request_id = 0;
        return 9;
    }
    return ++request_id;
}

#endif