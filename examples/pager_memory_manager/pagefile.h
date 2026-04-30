#ifndef PAGEFILE_H
#define PAGEFILE_H

#include "types.h"

static int pagefile_size = 0;
static int pagefile_freed_slots_stack[4000];
static int pagefile_stack_ptr = 0;

static inline int get_pagefile_slot() {
    if (pagefile_stack_ptr) {
        // sddf_printf("pagefile slot num is %d\n", pagefile_freed_slots_stack[pagefile_stack_ptr - 1]);
        return pagefile_freed_slots_stack[--pagefile_stack_ptr];
    }
    // sddf_printf("pagefile slot num is %d\n", pagefile_size);
    return pagefile_size++;
}

/**
 * Ensure that page->pagefile_offset is NOT -1!!!
 */
static inline void mark_pagefile_slot_free(pe *page) {
    pagefile_freed_slots_stack[pagefile_stack_ptr++] = page->pagefile_offset;
    page->pagefile_offset = -1;
}

// this is to store info to continue the things.
struct page_request_info page_continuations[10000];
static int request_id = 0;
int get_request_id() {
    if (request_id == 9999) {
        request_id = 0;
        return 9999;
    }
    return request_id++;
}

#endif