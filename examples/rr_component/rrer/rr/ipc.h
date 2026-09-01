#pragma once
#include "base.h"
#include "types.h"
#include "scheduler.h"
static inline void rr_init_ipc();
static inline microkit_channel rr_ch_to_target(microkit_channel ch);

static inline microkit_channel rr_ch_to_target(microkit_channel ch) {
    // we should never end up having channels equal to sender...
    assert(ch < sender_ch);
    if (ch % 2 == 0) return ch + 1;
    return ch - 1;
}

extern uint8_t* per_thread_recv_queue_mem;
extern seL4_Word per_thread_recv_queue_size;
queue_t* pt_recv_queue = NULL;
static inline void rr_init_ipc() {
    // just do naively for now.
    pt_recv_queue = (queue_t*) per_thread_recv_queue_mem;
    for (int i = 0; i * sizeof(queue_t) < per_thread_recv_queue_size; i++) {
        queue_init(per_thread_recv_queue_mem);
    }
}
