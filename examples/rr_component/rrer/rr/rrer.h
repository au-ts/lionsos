// the generic header.
#pragma once
#include "base.h"
#include "interfaces/sel4_client.h"
#include "scheduler.h"
#include "ipc.h"
#include "sel4/shared_types_gen.h"
#include "types.h"
static inline void rrer_main();
static inline void rrer_init();

// The index is the id. Points to mr_prefilled data.
static inline void rrer_init()
{
    assert(blocker_ch != UNSET_VALUE);
    assert(sender_ch != UNSET_VALUE);
    // initialises the children.
    rr_children_num = ((seL4_Word *)children_data_mem)[0];
    rr_children_arr = (rr_Child_t *)&((seL4_Word *)children_data_mem)[1];
    rr_children_sched_queue = (rr_Child_t **)&rr_children_arr[rr_children_num];
    LOG("children_arr: %p, children_num: %lu, children_sched_queue: %p\n", rr_children_arr, rr_children_num,
        rr_children_sched_queue);
    rr_init_scheduler();
    rr_init_ipc();
    LOG("blocker_ch: %lu, sender_ch: %lu\n", blocker_ch, sender_ch);

    // initialise the vpmu to be recording and on.
    seL4_ARM_VPMU_VPMUNumCounters_t counters = seL4_ARM_VPMU_VPMUNumCounters(VPMU_CAP);
    NO_ERR(counters.error);
    assert(counters.num_counters > 0);
    NO_ERR(seL4_ARM_VPMU_VPMUCounterControl(VPMU_CAP, 1));
}

static inline void rrer_perform_schedule()
{
    LOG("Performing schedule\n");
    // attempt to choose a nice next thread.
    for (rr_Child_t **child_arr_ptr = rr_sched_iterate(NULL); child_arr_ptr != NULL;
         child_arr_ptr = rr_sched_iterate(child_arr_ptr)) {
        switch (RR_CHILDSTATE_ENUM(child_arr_ptr[0]->sched_state)) {
        case rr_ChildState_Schedulable: {
            // Choose the thread
            // Has to be "chosen" var because the array has now changed.
            rr_Child_t *chosen = rr_sched_choose_child(child_arr_ptr);
            LOG("Chosen id: %lu\n", chosen->id);

            // Setup sender thread
            rr_ipc_sender_setup(chosen->id);
            // Setup block checker thread
            rr_sched_setup_block_checker();
            // and then do nothing.
            return;
        } break;
        case rr_ChildState_Scheduled: {
            assert(!"Unreachable");
        } break;
        case rr_ChildState_BlockedOnSend: {
            // cannot unblock until the target has been scheduled.
            // otherwise we might infinitely queue up the recv queue.
            continue;
        } break;
        case rr_ChildState_BlockedOnRecv: {
            // if there is no one sending to this thread we just clear it.
            if (rr_ipc_child_queue_len(child_arr_ptr[0]->id) == 0)
                continue;
            rr_Child_t *chosen = rr_sched_choose_child(child_arr_ptr);
            LOG("Chosen id: %lu\n", chosen->id);
            seL4_Word badge = rr_ipc_child_queue_peek_badge(chosen->id);
            seL4_Word source = BADGE_GET_PD_ID(badge);
            assert(source < rr_children_num);
            // mark the source as no longer blocked.
            rr_children_arr[source].sched_state = rr_ChildState_Schedulable;

            // setup sender thread
            rr_ipc_sender_setup(chosen->id);
            // setup block checker.
            rr_sched_setup_block_checker();
            return;
        } break;
        case _rr_ChildState_: {
            assert(!"Unreachable");
        } break;
        }
    }
    // this means no one is schedulable?
    // For now i'll treat this as invalid.
    assert(false);
}

// main
static inline void rrer_main()
{
    for (int i = 0; i < rr_children_num; i++) {
        LOG("id: %lu, priority: %lu, sched_state: %lu\n", rr_children_sched_queue[i]->id,
            rr_children_sched_queue[i]->priority, (seL4_Word)rr_children_sched_queue[i]->sched_state);
    }

    // schedule the first thread
    // ...
    rrer_perform_schedule();

    seL4_Word badge = 0;
    seL4_MessageInfo_t msg = { 0 };
    seL4_Word source_child = NO_CHILD;
    seL4_Word last_cycle_count = 0;
    while (true) {
        LOG("Yielding!\n");
        msg = seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
        LOG("Woken!\n");

        source_child = rr_ipc_get_child(badge);
        rr_IPCType_e type = rr_ipc_get_type(msg, badge);
        // read the cycle count
        seL4_ARM_VPMU_VPMUReadCycleCounter_t vpmu_res = seL4_ARM_VPMU_VPMUReadCycleCounter(VPMU_CAP);
        NO_ERR(vpmu_res.error);
        seL4_Word cycle_count = vpmu_res.cycle_counter_value;
        LOG("source_child: %lu, type: %d\n", source_child, type);
        LOG("last cycle count: %lu, cur cycle count: %lu\n", last_cycle_count, cycle_count);
        switch (type) {
        case rr_IPCType_BlockChecker: {
            // if time did not progress, mark currently scheduled as blocked by recv.
            if (cycle_count == last_cycle_count)
                rr_sched_unschedule_current(rr_ChildState_BlockedOnRecv);
            // Otherwise set it as schedulable.
            else
                rr_sched_unschedule_current(rr_ChildState_Schedulable);

            // perform a reschedule.
            rrer_perform_schedule();
        } break;
        case rr_IPCType_Msg: {
            // if type is msg, mark currently scheduled as blocked by send.
            // intentional fall-through
            assert(source_child != NO_CHILD);
            rr_sched_unschedule_current(RR_CHILDSTATE_SET_VALUE(rr_ChildState_BlockedOnSend, source_child));
        }
        case rr_IPCType_Ntfn: {
            if (type != rr_IPCType_Msg) {
                rr_sched_unschedule_current(rr_ChildState_Schedulable);
            }
            // store the message in the target's recv queue.
            rr_ipc_store_ipc_msg(source_child, msg, badge);

            // perform a reschedule.
            rrer_perform_schedule();
        } break;
        }
        last_cycle_count = cycle_count;
    }
}
