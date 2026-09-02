#pragma once
#include "base.h"
#include "interfaces/sel4_client.h"
#include "ipc.h"

static inline void rr_init_scheduler();

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

static inline rr_Child_t* rr_sched_choose_child(rr_Child_t **cur) {
    assert(cur != NULL);
    assert(cur >= rr_children_sched_queue);
    assert(cur < rr_children_sched_queue + rr_children_num);
    // swap the position of this value continuously until it encounters
    // a child with less priority or the end of the queue.
    while (cur + 1 < rr_children_sched_queue + rr_children_num && cur[0]->priority == cur[1]->priority)
    {
        rr_Child_t* temp = cur[0];
        cur[0] = cur[1];
        cur[1] = temp;
        cur++;
    }
    // sets up the correct priority of the child.
    rr_currently_sched = *cur;
    seL4_TCB_SetPriority(TCB(cur[0]->id), SELF_TCB(), SCHED_PRIO);

    // also assigns the vpmu.
    NO_ERR(seL4_TCB_BindVPMU(TCB(rr_currently_sched->id), VPMU_CAP));

    return rr_currently_sched;
}


static inline rr_Child_t* rr_sched_unschedule_current(rr_ChildState_e state) {
    seL4_TCB_SetPriority(TCB(rr_currently_sched->id), SELF_TCB(), SCHED_PRIO);
    NO_ERR(seL4_TCB_UnbindVPMU(TCB(rr_currently_sched->id)));
    rr_currently_sched->sched_state = state;
    rr_Child_t* temp = rr_currently_sched;
    rr_currently_sched = NULL;
    return temp;
}

static inline void rr_sched_setup_block_checker() {
    NO_ERR(seL4_TCB_Suspend(TCB(BLOCK_CHECKER_ID)));
    microkit_pd_restart(BLOCK_CHECKER_ID, BLOCK_CHECKER_ENTRY_POINT);
    NO_ERR(seL4_TCB_Resume(TCB(BLOCK_CHECKER_ID)));
}
