#pragma once
#include "base.h"

static inline void rrer_init_scheduler_();
static inline rr_Child_t* rrer_sched_q_pop();
static inline void rrer_schedule(rr_Child_t* child);
static inline void rrer_unschedule();
static inline void rrer_reschedule();

static inline void rrer_init_scheduler_() {
    // We do not unschedule the children, but they can never run because our priority is higher.
    // This ensures that a send to a "suspended" tcb is always received.
    // ASSUMPTION: NOT SMP KERNEL!!!
    // Populate the schedule queue (but not sort it)
    for (int i = 0; i < rr_children_num; i++) {
        rr_children_sched_queue[i] = &rr_children_arr[i];
        LOG(
            "id: %lu, priority: %lu, sched_state: %lu\n", 
            rr_children_sched_queue[i]->id,
            rr_children_sched_queue[i]->priority,
            (seL4_Word)rr_children_sched_queue[i]->sched_state
        );
    }

    // Sort the schedule queue.
    // funny bubble sort
    for (int up = rr_children_num - 1; up >= 0; up--) {
        for (int i = 0; i < up - 1; i++) {
            if (rr_children_sched_queue[i]->priority < rr_children_sched_queue[i + 1]->priority)
            {
                rr_Child_t* temp = rr_children_sched_queue[i];
                rr_children_sched_queue[i] = rr_children_sched_queue[i + 1];
                rr_children_sched_queue[i + 1] = temp;
            }
        }
    }
}

static inline rr_Child_t* rrer_sched_q_pop() {
    // choose the next highest priority, currently schedulable child
    // and then move it to the end of the queue for that specific priority level.
    // and sets that child to be scheduled.
    // Returns NULL if no one can be scheduled.
    rr_Child_t* child = NULL;

    for (int i = 0; i < rr_children_num; i++) {
        if (rr_children_sched_queue[i]->sched_state == rr_ChildState_Schedulable) {
            child = rr_children_sched_queue[i];
            int j = i + 1;
            for (; 
                j < rr_children_num && rr_children_sched_queue[j]->priority == child->priority;
                j++) 
            {
                rr_children_sched_queue[j-1] = rr_children_sched_queue[j];
            }
            rr_children_sched_queue[j-1] = child;
            LOG("Found child to schedule: id %lu\n", child->id);
            break;
        }
    }
    return child;
}

static inline void rrer_schedule(rr_Child_t* child) {
    assert(child != NULL);
    child->sched_state = rr_ChildState_Scheduled;

    // vpmu is guaranteed not to be bound.
    NO_ERR(seL4_TCB_BindVPMU(TCB(child->id), VPMU_CAP));

    NO_ERR(seL4_TCB_SetPriority(TCB(child->id), SELF_TCB(), 253));
}

static inline void rrer_unschedule() {
    assert(rr_currently_sched != NULL);
    NO_ERR(seL4_TCB_SetPriority(TCB(rr_currently_sched->id), SELF_TCB(), rr_currently_sched->priority));
    rr_currently_sched->sched_state = rr_ChildState_Blocked;
    NO_ERR(seL4_TCB_UnbindVPMU(TCB(rr_currently_sched->id)));
    rr_currently_sched = NULL;
}

static inline void rrer_reschedule() {
    rrer_unschedule();

    // try to schedule another thread.
    rr_Child_t* next = rrer_sched_q_pop();
    // if NULL then we are currently completely blocked.
    assert(next != NULL);

    rrer_schedule(next);
}
