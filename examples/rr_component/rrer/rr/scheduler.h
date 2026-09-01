#pragma once
#include "base.h"

static inline void rr_init_scheduler();
static inline void rr_schedule(rr_Child_t *child);
static inline void rr_unschedule();
static inline void rr_reschedule();

static inline void rr_init_scheduler()
{
    // We do not suspend the children, but they can never run because our priority is higher.
    // We could suspend them now, because we are controlling IPC such that they will receive it
    // automatically when they are scheduled.
    for (int i = 0; i < rr_children_num; i++) {
        rr_children_sched_queue[i] = &rr_children_arr[i];
        LOG("id: %lu, priority: %lu, sched_state: %lu\n", rr_children_sched_queue[i]->id,
            rr_children_sched_queue[i]->priority, (seL4_Word)rr_children_sched_queue[i]->sched_state);
    }

    // Sort the schedule queue.
    // funny bubble sort
    for (int up = rr_children_num - 1; up >= 0; up--) {
        for (int i = 0; i < up - 1; i++) {
            if (rr_children_sched_queue[i]->priority < rr_children_sched_queue[i + 1]->priority) {
                rr_Child_t *temp = rr_children_sched_queue[i];
                rr_children_sched_queue[i] = rr_children_sched_queue[i + 1];
                rr_children_sched_queue[i + 1] = temp;
            }
        }
    }
}

// NULL gives the first thread.
// if NULL is returned then we have run out of threads.
static inline rr_Child_t** rr_sched_iterate(rr_Child_t** cur) {
    if (cur == NULL) {
        return rr_children_sched_queue;
    }
    else if (cur + 1 >= rr_children_sched_queue + rr_children_num) return NULL;
    return cur + 1;
}

static inline rr_Child_t** rr_sched_choose_child(rr_Child_t **cur) {
    assert(cur != NULL);
    assert(cur >= rr_children_sched_queue);
    assert(cur < rr_children_sched_queue + rr_children_num);
    // swap the position of this value continuously until it encounters
    // a child with less priority.
    while (cur + 1 < rr_children_sched_queue + rr_children_num && cur[0]->priority == cur[1]->priority)
    {
        rr_Child_t* temp = cur[0];
        cur[0] = cur[1];
        cur[1] = temp;
        cur++;
    }
}
