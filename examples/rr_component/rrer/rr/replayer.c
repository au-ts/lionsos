/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "base.h"
#include "init.h"
#include "interfaces/sel4_client.h"
#include "microkit.h"
#include "scheduler.h"
#include "ipc.h"
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "sel4/syscalls_mcs.h"
#include "storage_backend/base.h"
#include "storage_backend/block.h"
#include "types.h"
#include "fault.h"
#include <sddf/util/printf.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

serial_queue_t *tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

void rep_main()
{

    seL4_Word last_cycle_count = 0;
    seL4_MessageInfo_t msg = { 0 };
    seL4_Word badge = 0;
    seL4_Word true_cycle_count = 0;
    // idk how I should do this.
    for (int inst_num = 0; inst_num < _rr_storage_handle.metadata.inst_num; inst_num++) {
        rr_storage_unit_t inst = rr_storage_get_next_inst(&_rr_storage_handle);
        if (last_cycle_count != inst.cycle_count) {
            rr_sched_schedule_child(rr_currently_sched);
            NO_ERR(seL4_TCB_Resume(rr_currently_sched->id + BASE_TCB_CAP));
            rr_ipc_sender_setup(rr_currently_sched->id);
            rr_sched_setup_block_checker();
            // setup for this cycle count has been done, so we will setup everything else and yield.
            // wait for preemption.
            msg = seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
            seL4_ARM_VPMU_VPMUReadCycleCounter_t vpmu_res = seL4_ARM_VPMU_VPMUReadCycleCounter(VPMU_CAP);
            NO_ERR(vpmu_res.error);
            true_cycle_count = vpmu_res.cycle_counter_value;
            assert(true_cycle_count == inst.cycle_count);

            // We must unschedule the thread to what the corresponding state should be after this type of message.
            // These will be overwritten later, it's just to ensure that the correct shutdown of a scheduled
            // thread occurs.
            switch (rr_ipc_get_type(msg, badge)) {
            case rr_IPCType_BlockChecker: {
                // schedulable -> if it were truly blockedByRecv we would set it later.
                rr_sched_unschedule_current(rr_ChildState_Schedulable);
            } break;
            case rr_IPCType_Ntfn: {
                // schedulable -> Ntfns are non-blocking but act as preemption points
                rr_sched_unschedule_current(rr_ChildState_Schedulable);
            } break;
            case rr_IPCType_Msg: {
                // blockedBySend but is actually unreachable
                assert(!"Unreachable");
            } break;
            case rr_IPCType_Call: {
                // blockedByCall -> duh
                rr_sched_unschedule_current(rr_ChildState_BlockedOnCall);
            } break;
            case rr_IPCType_Fault: {
                assert(!"Todo!");
            } break;
            case rr_IPCType_SenderReply: {
                assert(!"Todo!");
            } break;
            }
        }

        rr_storage_unit_print(&inst);

        // Normally every IPC is a preemption point
        // After being preempted the scheduler makes the current thread schedulable and at the back of the Q.
        // And then we process tne notification.
        // For a collection of instruction at the same timepoint, we run through them all, and then sleep until
        // a preemption point.
        // We then run through the next set of instructions at the next cycle count, and we use the preemption IPC
        // to solve IPC assertions/divertions.
        // What about blockchecker preemptions? We need this for blockedonrecv
        switch (inst.unit_type) {
        case rr_storage_unit_REPLY: {
            assert(!"TODO!");
        } break;
        case rr_storage_unit_MSG: {
            assert(!"Unreachable");
        } break;
        case rr_storage_unit_CALL: {
            assert(!"TODO!");
        } break;
        case rr_storage_unit_NTFN: {
            // check if we have diverged
            assert(rr_ipc_get_type(msg, badge) == rr_IPCType_Ntfn);
            assert(rr_badge_to_channel_id(badge) == inst.args.ntfn_args.source_chan);
            assert(rrer_source_ch_to_target_ch(rr_badge_to_channel_id(badge)) == inst.args.ntfn_args.target_chan);
            assert(msg.words[0] == inst.args.ntfn_args.msginfo.words[0]);
            // Send the corresponding message to the child.
            rr_ipc_store_ipc_msg(rr_channel_to_target_child_id[inst.args.ntfn_args.source_chan],
                                 inst.args.ntfn_args.msginfo, inst.args.ntfn_args.badge,
                                 inst.args.ntfn_args.source_chan);
        } break;
        case rr_storage_unit_SCHED: {
            // if changing the state to scheduled we update the rr_currently_sched accordingly
            if (inst.args.sched_args.state == rr_ChildState_Scheduled) {
                rr_currently_sched = &rr_children_arr[inst.args.sched_args.child];
            }
            rr_children_arr[inst.args.sched_args.child].sched_state = inst.args.sched_args.state;
        } break;
        case _rr_storage_unit_max:
        default: {
            assert(!"Unreachable");
        } break;
        }
        last_cycle_count = inst.cycle_count;
    }
}

void init()
{
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    INFO("INIT\n");
    rr_init(false);
    INFO("Init complete!\n");
    rep_main();
}

void notified(microkit_channel ch)
{
    INFO("Notified! %d\n", ch);
    // unreachable
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    INFO("Protected!\n");
    // unreachable
    return msginfo;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    *reply_msginfo = microkit_msginfo_new(0, 0);
    // unreachable.
    return false;
}
