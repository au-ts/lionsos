/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * Copyright 2019 Adventium Labs
 * Modifications made to original
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_Adventium_BSD)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

// TODO:
// - Capacity (how to handle queue being full)

#define NUM_ENTRIES 64
// Represents that a buffer was the first to be transmitted
#define FIRST_TXD (NUM_ENTRIES + 1)

uint8_t outgoing_buffers[NUM_ENTRIES];
uint8_t receive_queue[NUM_ENTRIES];

typedef struct queue {
    /* next index to allocate */
    uint16_t head;
    /* free buffers */
    uint16_t free[NUM_ENTRIES];
    /* last buffer transmitted */
    uint16_t last_txd;
    /* indices */
    uint16_t prev_txd[NUM_ENTRIES];
} queue_t;

queue_t queue;

// assumes queue is not empty
uint16_t alloc_buffer() {
    uint16_t head = queue.head;
    while (a_cas_p(&queue.head, head, head + 1)) {
        head = queue.head;
    }
    return queue.free[head];
}

void tx_buffer(uint16_t buffer) {
    uint16_t last_txd = queue.last_txd;
    // TODO: if buffer in pending, can't clear this
    queue.prev_txd[buffer] = last_txd;
    while (a_cas_p(&queue.last_txd, last_txd, buffer)) {
        last_txd = queue.last_txd;
        queue.prev_txd[buffer] = last_txd;
    }
}

// sPD book-keeping

typedef struct pending_node {
    uint16_t buffer;
    uint16_t prev_transmitted;
    uint8_t next_hole;
    uint8_t child;
} pending_node_t;

#define NUM_A_PDS 8

// Buffers from last schedule that have been alloced, but not transmitted
pending_node_t pending[NUM_A_PDS];
uint8_t num_pending;

// Index of each buffer in the pending map
uint8_t pending_map[NUM_ENTRIES];

void init(void) {
    queue.head = 0;
    queue.last_transmitted = NUM_ENTRIES;
    for (uint16_t i = 0; i < NUM_ENTRIES; i++) {
        queue.next[i] = i + 1;
        queue.prev_transmitted[i] = NUM_ENTRIES;
        pending_map[i] = NUM_A_PDS;
    }
}

void process_partition(void) {
    // TODO: have to check properly if it was *actually* transmitted by checking the value of last transmitted
    // Check if last allocated was transmitted
    uint16_t last_alloced = queue.head - 1;
    while (pending_map[last_alloced] != NUM_A_PDS) {
        last_alloced--;
    }

    if (queue.prev_transmitted[last_alloced] == NUM_ENTRIES || queue.prev_transmitted[last_alloced] == queue.last_transmitted) {
        // if last alloced not transmitted, add to pending
    }

    uint16_t last_transmitted = last_alloced;
    while (pending_map[last_transmitted] != NUM_A_PDS && queue.prev_transmitted[last_transmitted] != NUM_ENTRIES) {
        last_transmitted--;
    }

    uint8_t pending_idx = 0;
    uint8_t hole_head = NUM_A_PDS;
    while (pending_idx < num_pending) {
        uint16_t pending_buffer = pending[pending_idx].buffer;
        uint16_t prev_transmitted = queue.prev_transmitted[pending_buffer];

        // If pending has not been transmitted, ignore for now
        if (prev_transmitted == NUM_ENTRIES || prev_transmitted == queue.last_transmitted) {
            continue;
        }

        // Previously transmitted is not pending, add to ordered hole queue
        if (pending_map[prev_transmitted] == NUM_A_PDS) {
            if (hole_head == NUM_A_PDS) {
                hole_head = pending_idx;
                continue;
            } else {
                uint8_t prev_idx = NUM_A_PDS;
                uint8_t idx = hole_head;
                while (idx != NUM_A_PDS) {
                    if (pending[idx].prev_transmitted < prev_transmitted) {
                        prev_idx = idx;
                        idx = pending[idx].next_hole;
                        continue;
                    }

                    pending[pending_idx].next_hole = idx;
                    if (prev_idx != NUM_A_PDS) {
                        pending[prev_idx].next_hole = pending_idx;
                    } else {
                        hole_head = pending_idx;
                    }
                }

                if (idx == NUM_A_PDS) {
                    // Node is the new tail
                    pending[idx].next_hole = pending_idx;
                }
            }
        } else {
            // Previously transmitted was a pending, add as a child
            pending[pending_map[prev_transmitted]].child = pending_idx;
        }

        pending_idx++;
    }

    // COPY
    uint16_t buffers_copied = 0;
    uint16_t next_to_copy = 0;
    uint8_t hole_idx = 0;
    pending_idx = 0;
    do {
        bool insert = false;
        bool hole = false;
        uint8_t stop_idx = last_transmitted;

        // missing buffer
        if (pending_idx < num_pending && pending[pending_idx].buffer < stop_idx) {
            stop_idx = pending[pending_idx].buffer;
            hole = true;
        }

        // out of order insertion
        if (hole_idx != NUM_A_PDS && pending[hole_idx].prev_transmitted < stop_idx) {
            stop_idx = pending[hole_idx].prev_transmitted + 1;
            insert = true;
        }

        memcpy(&receive_queue[buffers_copied], &outgoing_buffers[next_to_copy], stop_idx - next_to_copy);
        buffers_copied += stop_idx - next_to_copy;
        next_to_copy = stop_idx - next_to_copy;

        if (hole && !insert) {
            // simple buffer removal
            next_to_copy++;
            pending_idx++;
        }

        if (insert) {
            // copy in out of order chain
            uint8_t child_idx = hole_idx;
            do {
                memcpy(&receive_queue[buffers_copied], &outgoing_buffers[pending[child_idx].buffer], 1);
                buffers_copied++;
            } while (pending[child_idx].child);

            hole_idx = pending[hole_idx].next_hole;
        }

    } while (next_to_copy < last_transmitted);
}
