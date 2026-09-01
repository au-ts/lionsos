// the generic header.
#pragma once
#include "base.h"
#include "scheduler.h"
#include "ipc.h"
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
    rr_init_scheduler();
    rr_init_ipc();

    // initialise the vpmu to be recording and on.
    seL4_ARM_VPMU_VPMUNumCounters_t counters = seL4_ARM_VPMU_VPMUNumCounters(VPMU_CAP);
    NO_ERR(counters.error);
    assert(counters.num_counters > 0);
    NO_ERR(seL4_ARM_VPMU_VPMUCounterControl(VPMU_CAP, 1));
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
}

