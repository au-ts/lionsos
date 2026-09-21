#pragma once
#include "base.h"
#include "interfaces/sel4_client.h"
#include "ipc.h"
#include "microkit.h"

static inline void rr_init_scheduler();

static inline void rr_init_scheduler()
{
    // We only suspend children who are correspondingly in a suspended state, or who
    // are schedulable but not running
    // Since everyone starts schedulable, everyone is suspended
    // This ensures that those who are being replied to will receive the reply non-blocking.
    for (int i = 0; i < rr_children_num; i++) {
        rr_children_sched_queue[i] = &rr_children_arr[i];
        NO_ERR(seL4_TCB_Suspend(rr_children_arr[i].id + BASE_TCB_CAP));
    }

    // Sort the schedule queue.
    // funny bubble sort
    for (int up = rr_children_num - 1; up >= 0; up--) {
        for (int i = 0; i < up; i++) {
            if (rr_children_sched_queue[i]->priority < rr_children_sched_queue[i + 1]->priority) {
                rr_Child_t *temp = rr_children_sched_queue[i];
                rr_children_sched_queue[i] = rr_children_sched_queue[i + 1];
                rr_children_sched_queue[i + 1] = temp;
            }
        }
    }
    for (int i = 0; i < rr_children_num; i++) {
        INFO("id: %lu, priority: %lu, sched_state: %lu\n", rr_children_sched_queue[i]->id,
            rr_children_sched_queue[i]->priority, (seL4_Word)rr_children_sched_queue[i]->sched_state);
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

    // if the child was schedulable, then it was suspended, so unsuspend it
    if (rr_currently_sched->sched_state == rr_ChildState_Schedulable)
        NO_ERR(seL4_TCB_Resume(rr_currently_sched->id + BASE_TCB_CAP));

    // set the correct state.
    rr_currently_sched->sched_state = rr_ChildState_Scheduled;
    seL4_TCB_SetPriority(TCB(cur[0]->id), SELF_TCB(), SCHED_PRIO);

    // also assigns the vpmu.
    NO_ERR(seL4_TCB_BindVPMU(TCB(rr_currently_sched->id), VPMU_CAP));

    return rr_currently_sched;
}


static inline rr_Child_t* rr_sched_unschedule_current(rr_ChildState_e state) {
    seL4_TCB_SetPriority(TCB(rr_currently_sched->id), SELF_TCB(), rr_currently_sched->priority);
    // if we are changing the state to suspended, then we will suspend the thread as well.
    if (state == rr_ChildState_Suspended) 
        NO_ERR(seL4_TCB_Suspend(rr_currently_sched->id + BASE_TCB_CAP));
    NO_ERR(seL4_TCB_UnbindVPMU(TCB(rr_currently_sched->id)));
    rr_currently_sched->sched_state = state;
    rr_Child_t* temp = rr_currently_sched;
    rr_currently_sched = NULL;
    rr_last_sched_child = temp;
    return temp;
}

static inline void rr_sched_setup_block_checker() {
    microkit_notify(blocker_ch);
}
