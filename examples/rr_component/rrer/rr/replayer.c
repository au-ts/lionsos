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

seL4_Word stored_ipc_message_buffer[64];

void rep_main()
{
    for (int inst_num = 0; inst_num < _rr_storage_handle.metadata.inst_num; inst_num++) {
        rr_storage_unit_t inst = rr_storage_get_next_inst(&_rr_storage_handle);
        rr_storage_unit_print(&inst);
    }
    _rr_storage_handle.iptr = _rr_storage_handle.metadata.inst_begin;

    seL4_Word last_cycle_count = 0;
    seL4_MessageInfo_t msg = { 0 };
    seL4_Word badge = 0;
    seL4_Word true_cycle_count = 0;
    bool from_reply = false;
    rr_Child_t* to_schedule = NULL;
    // idk how I should do this.
    for (int inst_num = 0; inst_num < _rr_storage_handle.metadata.inst_num; inst_num++) {
        rr_storage_unit_t inst = rr_storage_get_next_inst(&_rr_storage_handle);
        if (last_cycle_count != inst.cycle_count) {
            // sus, maybe should switch to FSM model.
            if (!from_reply) {
                // Now we reschedule the child.
                rr_sched_schedule_child(to_schedule);
                to_schedule = NULL;
                rr_ipc_sender_setup(rr_currently_sched->id);
                rr_sched_setup_block_checker();
                // The TCB may or may not be suspended.
                NO_ERR(seL4_TCB_Resume(rr_currently_sched->id + BASE_TCB_CAP));
            }
            from_reply = false;
            // setup for this cycle count has been done, so we will setup everything else and yield.
            // wait for preemption.
            msg = seL4_Recv(INPUT_CAP, &badge, BASE_REPLY_CAPS + rr_currently_sched->id);
            NO_ERR(seL4_TCB_Suspend(BLOCK_CHECKER_ID + BASE_TCB_CAP));
            INFO("Recv'd message\n");

            seL4_ARM_VPMU_VPMUReadCycleCounter_t vpmu_res = seL4_ARM_VPMU_VPMUReadCycleCounter(VPMU_CAP);
            NO_ERR(vpmu_res.error);
            true_cycle_count = vpmu_res.cycle_counter_value;
            if (true_cycle_count != inst.cycle_count) {
                WARN("Cycle count divergence! true: %lx, expected %lx\n", true_cycle_count, inst.cycle_count);
                assert(!"Cycle count divergence!");
            }

            // We must unschedule the thread to what the corresponding state should be after this type of message.
            // These will be overwritten later, it's just to ensure that the correct shutdown of a scheduled
            // thread occurs.
            switch (rr_ipc_get_type(msg, badge)) {
            case rr_IPCType_BlockChecker: {
                // We must determine if it's blockedByRecv or Schedulable.
                // in the case it is schedulable we must suspend.
                // This is so we can read the block driver.
                if (true_cycle_count - last_cycle_count < 3) {
                    rr_sched_unschedule_current(rr_ChildState_BlockedOnRecv);
                }
                else {
                    rr_sched_unschedule_current(rr_ChildState_Schedulable);
                }
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
                // for now.
                rr_sched_unschedule_current(rr_ChildState_Suspended);
            } break;
            case rr_IPCType_SenderReply: {
                // schedulable.
                rr_sched_unschedule_current(rr_ChildState_Schedulable);
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
            // check diversion
            assert(rr_ipc_get_type(msg, badge) == rr_IPCType_SenderReply);
            assert(msg.words[0] == inst.args.reply_args.msginfo.words[0]);

            // get the data and store it in the msg buffer.
            seL4_Word msglen = seL4_MessageInfo_get_length(inst.args.reply_args.msginfo);
            rr_storage_read(inst.args.reply_args.data_offset, (uint8_t*)stored_ipc_message_buffer, msglen * sizeof(seL4_Word));
            seL4_MessageInfo_t msginfo = seL4_MessageInfo_new(seL4_MessageInfo_get_label(inst.args.reply_args.msginfo), 0, 0, msglen);
            for (int i = 0; i < msglen; i++) {
                seL4_SetMR(i, stored_ipc_message_buffer[i]);
            }

            // This will transfer control early, so we need to do all the pre-ctxt switch bullshit beforehand.
            last_cycle_count = inst.cycle_count;
            from_reply = true;

            rr_sched_schedule_child(&rr_children_arr[inst.args.reply_args.target_child]);
            assert(inst.args.reply_args.target_child == rr_currently_sched->id);
            rr_ipc_sender_setup(rr_currently_sched->id);
            rr_sched_setup_block_checker();
            // sus?
            // NO_ERR(seL4_TCB_Resume(rr_currently_sched->id + BASE_TCB_CAP));
            seL4_Send(BASE_REPLY_CAPS + inst.args.reply_args.target_child, msginfo);
            continue;
        } break;
        case rr_storage_unit_MSG: {
            assert(!"Unreachable");
        } break;
        case rr_storage_unit_CALL: {
            // We do not expect the messages being sent are the same.
            // "Intercept" the value being sent and replace it with the one being recorded.
            // this is so that hopefully execution remains as expected.

            // Roundabout way.
            // TODO: refactor this.
            seL4_Word sourceId = rr_channel_to_target_child_id[inst.args.call_args.target_chan];
            seL4_Word targetId = rr_channel_to_target_child_id[inst.args.call_args.source_chan];
            assert(sourceId == rr_last_sched_child->id);

            // check diversion
            assert(rr_ipc_get_type(msg, badge) == rr_IPCType_Call);
            assert((badge & CHANNEL_MASK) == inst.args.msg_args.source_chan);
            assert(rrer_source_ch_to_target_ch(badge & CHANNEL_MASK) == inst.args.msg_args.target_chan);
            assert(msg.words[0] == inst.args.msg_args.msginfo.words[0]);

            // get the data and store it in the msg buffer.
            seL4_Word msglen = seL4_MessageInfo_get_length(inst.args.msg_args.msginfo);
            rr_storage_read(inst.args.msg_args.data_offset, (uint8_t*)stored_ipc_message_buffer, msglen * sizeof(seL4_Word));
            seL4_MessageInfo_t msginfo = seL4_MessageInfo_new(seL4_MessageInfo_get_label(inst.args.msg_args.msginfo), 0, 0, msglen);
            for (int i = 0; i < msglen; i++) {
                seL4_SetMR(i, stored_ipc_message_buffer[i]);
            }

            // transfer the message into the sender message queue.
            rr_ipc_store_ipc_msg(targetId,
                                 msginfo, inst.args.call_args.badge,
                                 inst.args.call_args.source_chan);
        } break;
        case rr_storage_unit_NTFN: {
            // check if we have diverged
            assert(rr_ipc_get_type(msg, badge) == rr_IPCType_Ntfn);
            assert(rr_badge_to_channel_id(badge) == inst.args.ntfn_args.source_chan);
            assert(rrer_source_ch_to_target_ch(rr_badge_to_channel_id(badge)) == inst.args.ntfn_args.target_chan);
            // this is msginfo, not the actual data.
            assert(msg.words[0] == inst.args.ntfn_args.msginfo.words[0]);
            // Send the corresponding message to the child.
            rr_ipc_store_ipc_msg(rr_channel_to_target_child_id[inst.args.ntfn_args.source_chan],
                                 inst.args.ntfn_args.msginfo, inst.args.ntfn_args.badge,
                                 inst.args.ntfn_args.source_chan);
        } break;
        case rr_storage_unit_SCHED: {
            // if changing the state to scheduled we update the to_schedule accordingly
            if (inst.args.sched_args.state == rr_ChildState_Scheduled) {
                to_schedule = &rr_children_arr[inst.args.sched_args.child];
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
    INFO("!!! Finished Replaying !!!\n");
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
