// the generic header.
#pragma once
#include "base.h"
#include "scheduler.h"
#include "ipc.h"
#include "interfaces/sel4_client.h"
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "sel4/syscalls_mcs.h"
static inline void rrer_main();
static inline void rrer_init(seL4_Word* data);


// The index is the id. Points to mr_prefilled data.
static inline void rrer_init(seL4_Word* data) {
    // initialises the children.
    rr_children_num = data[0];
    rr_children_arr = (rr_Child_t*)&data[1];
    rr_children_sched_queue = (rr_Child_t**)&rr_children_arr[rr_children_num];
    LOG(
        "children_arr: %p, children_num: %lu, children_sched_queue: %p\n", 
        rr_children_arr,
        rr_children_num, 
        rr_children_sched_queue
    );
    rrer_init_scheduler_();

    // initialise the vpmu to be recording and on.
    seL4_ARM_VPMU_VPMUNumCounters_t counters = seL4_ARM_VPMU_VPMUNumCounters(VPMU_CAP);
    NO_ERR(counters.error);
    assert(counters.num_counters > 0);
    NO_ERR(seL4_ARM_VPMU_VPMUCounterControl(VPMU_CAP, 1));
}

static inline seL4_Word rrer_cyclecount_diff_(seL4_Word old_value) {
    seL4_ARM_VPMU_VPMUReadCycleCounter_t vpmu_count = seL4_ARM_VPMU_VPMUReadCycleCounter(VPMU_CAP);
    NO_ERR(vpmu_count.error);

    int64_t diff = vpmu_count.cycle_counter_value - old_value;
    if (diff < 0) diff = -diff;

    // guaranteed to be positive.
    return (seL4_Word)diff;
}

// main
static inline void rrer_main() {
    for (int i = 0; i < rr_children_num; i++) {
        LOG(
            "id: %lu, priority: %lu, sched_state: %lu\n", 
            rr_children_sched_queue[i]->id,
            rr_children_sched_queue[i]->priority,
            (seL4_Word)rr_children_sched_queue[i]->sched_state
        );
    }

    while (true) {
        switch (rr_sched_state) {
            case rr_SchedState_None: {
                rr_Child_t* next = rrer_sched_q_pop();
                assert(next != NULL);
                rrer_schedule(next);
                rr_sched_state = rr_SchedState_Scheduled;
            } break;
            case rr_SchedState_Scheduled: {
                // block on a recv, and handle IPC.
                seL4_Word badge = 0;
                seL4_MessageInfo_t msg = seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
                // if notification, then reschedule
                // if endpoint, then we were sent to
                //   reschedule then 
            } break;
            case rr_SchedState_Blocked: {
            } break;
        }
    }
}

