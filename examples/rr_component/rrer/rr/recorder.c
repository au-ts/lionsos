// the generic header.
#include "base.h"
#include "interfaces/sel4_client.h"
#include "microkit.h"
#include "scheduler.h"
#include "ipc.h"
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "types.h"
#include "fault.h"
#include "init.h"
#include <sddf/util/printf.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>


static inline void rec_main();
static inline void rec_perform_schedule(seL4_Word cycle_count);


static inline void rec_perform_schedule(seL4_Word cycle_count)
{
    LOG("Performing schedule\n");
    // attempt to choose a nice next thread.
    for (rr_Child_t **child_arr_ptr = rr_sched_iterate(NULL); child_arr_ptr != NULL;
         child_arr_ptr = rr_sched_iterate(child_arr_ptr)) {
        switch (child_arr_ptr[0]->sched_state) {
        case rr_ChildState_Schedulable: {
            // Choose the thread
            // Has to be "chosen" var because the array has now changed.
            // store the event
            rr_Child_t *chosen = rr_sched_choose_child(child_arr_ptr);
            LOG("Chosen id: %lu\n", chosen->id);


            rr_storage_store_scheduler_event(&_rr_storage_handle, cycle_count, chosen->id,
                                            chosen->sched_state);
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
        case rr_ChildState_BlockedOnCall: {
            // Transitioned out when a blockedOnRecv has corresponding
            // BlockedOnCall, such that it now transitions to BlockedOnReply
            LOG("id: %lu - Ignoring blocked on Call\n", child_arr_ptr[0]->id);
        } break;
        case rr_ChildState_BlockedOnReply: {
            // We do not transition out of this state here. This is done
            // via two different possibilities:
            // 1. The replier is blocked by recv
            //    - Cancel the IPC
            //    - This is because it's impossible for the replier to recv on a different reply_cap
            //    - Which means it willfully ignored the call.
            // 2. The replier replied
            //    - We are notified of this via the sender completing its call.
            //    - The message received in the special reply_buffer is immediately sent
            //    - via the correct reply_cap. (this is non blocking so can be done here).
            LOG("id: %lu - Ignoring blocked on reply\n", child_arr_ptr[0]->id);
        } break;
        case rr_ChildState_BlockedOnSend: {
            // cannot unblock until the target has been scheduled.
            // otherwise we might infinitely queue up the recv queue.
            // Gets transitioned out by a matching blockedonrecv
            LOG("id: %lu - Ignoring blocked on send\n", child_arr_ptr[0]->id);
            continue;
        } break;
        case rr_ChildState_BlockedOnRecv: {
            // if there is no one sending to this thread we just clear it.
            if (rr_ipc_child_queue_len(child_arr_ptr[0]->id) == 0) {
                LOG("id: %lu - Ignoring blocked on recv because no msgs queued\n", child_arr_ptr[0]->id);
                continue;
            }
            LOG("id: %lu - Scheduling blocked on recv because msg was queued\n", child_arr_ptr[0]->id);

            rr_Child_t *chosen = rr_sched_choose_child(child_arr_ptr);

            LOG("Chosen id: %lu\n", chosen->id);
            seL4_Word source_channel = rr_ipc_child_queue_peek_channel(chosen->id);
            assert(source_channel < rr_channels_num);
            LOG("Source channel: %lu\n", source_channel);
            seL4_Word source_child = rr_source_ch_to_source_child(source_channel);
            assert(source_child < rr_children_num);
            LOG("Source child: %lu\n", source_child);

			rr_Child_t* source_child_state = &rr_children_arr[source_child];

            // mark the source as no longer blocked.
            if (source_child_state->sched_state == rr_ChildState_BlockedOnSend)
            {
                source_child_state->sched_state = rr_ChildState_Schedulable;
            }
            // mark a blocked on call as now blocked by reply.
            else if (source_child_state->sched_state == rr_ChildState_BlockedOnCall) {
                // This is so we can determine who to reply to.
                rr_recv_source_channel = source_channel;

                source_child_state->sched_state = rr_ChildState_BlockedOnReply;
            }
            // the only other case is for notification, in which it's schedulable.
            else if (source_child_state->sched_state != rr_ChildState_Schedulable) {
                ERR("Unexpected source child state %s\n",
                    rr_child_state_to_string(source_child_state->sched_state));
                assert(!"Unreachable");
            };
            // Store the changes made to the sender.
            rr_storage_store_scheduler_event(&_rr_storage_handle, cycle_count, source_child_state->id,
                                            source_child_state->sched_state);

            // store the event
            rr_storage_store_scheduler_event(&_rr_storage_handle, cycle_count, chosen->id,
                                            chosen->sched_state);

            // setup sender thread
            rr_ipc_sender_setup(chosen->id);
            // setup block checker.
            rr_sched_setup_block_checker();
            return;
        } break;
        case _rr_ChildState_: {
            assert(!"Unreachable");
        } break;
        case rr_ChildState_Suspended: {
            LOG("Child %lu is suspended, continuing\n", child_arr_ptr[0]->id);
        } break;
        }
    }
    // this means no one is schedulable?
    // For now i'll treat this as invalid.
    ERR("No one is schedulable\n");
    for (rr_Child_t **child_arr_ptr = rr_sched_iterate(NULL); child_arr_ptr != NULL;
         child_arr_ptr = rr_sched_iterate(child_arr_ptr)) {
        ERR("child_%lu %s\n", child_arr_ptr[0]->id, rr_child_state_to_string(child_arr_ptr[0]->sched_state));
    }
    assert(!"TODO: Think about what to do when no one is schedulable");
}

static inline void rec_unschedule_current(seL4_Word cycle_count, rr_ChildState_e new_state)
{
    seL4_Word last_sched_id = rr_currently_sched->id;
    rr_sched_unschedule_current(new_state);
    rr_storage_store_scheduler_event(&_rr_storage_handle, cycle_count, last_sched_id, new_state);
}

// main
static inline void rec_main()
{
    // schedule the first thread
    rec_perform_schedule(0);
    NO_ERR(seL4_TCB_Resume(rr_currently_sched->id + BASE_TCB_CAP));

    seL4_Word badge = 0;
    seL4_MessageInfo_t msg = { 0 };
    seL4_Word last_cycle_count = 0;
    while (true) {
        LOG("Yielding!\n");
        msg = seL4_Recv(INPUT_CAP, &badge, BASE_REPLY_CAPS + rr_currently_sched->id);
        LOG("Woken!\n");
        // pause the sender and the block checker.
        seL4_Word sending_child = NO_CHILD;

        rr_IPCType_e type = rr_ipc_get_type(msg, badge);
        // read the cycle count
        seL4_ARM_VPMU_VPMUReadCycleCounter_t vpmu_res = seL4_ARM_VPMU_VPMUReadCycleCounter(VPMU_CAP);
        NO_ERR(vpmu_res.error);
        seL4_Word cycle_count = vpmu_res.cycle_counter_value;
        LOG("msg type: %s\n", rr_ipc_type_to_string(type));
        switch (type) {
        case rr_IPCType_SenderReply: {
            // Determine the child to reply to.
            // The badge does not contain any useful information.
            // The label has to be forwarded.
            // We store the recv_source_channel for this reason.
            assert(rr_recv_source_channel != UNSET_VALUE);

            seL4_Word replyee_child_id =
                rr_source_ch_to_source_child(rr_recv_source_channel);
            assert(replyee_child_id < rr_children_num);

            // store the message
            rr_storage_store_ipc_msg(&_rr_storage_handle, cycle_count, rr_currently_sched->id, badge, msg);

            // We can send the reply as it won't block.
            seL4_Send(BASE_REPLY_CAPS + replyee_child_id, msg);
            // and then we should unschedule the current.
            rec_unschedule_current(cycle_count, rr_ChildState_Schedulable);

            // and also set the replied to pd as schedulable
            rr_children_arr[replyee_child_id].sched_state = rr_ChildState_Schedulable;
            rr_storage_store_scheduler_event(&_rr_storage_handle, cycle_count, replyee_child_id, rr_ChildState_Schedulable);

            rr_recv_source_channel = UNSET_VALUE;
        } break;
        case rr_IPCType_Fault: {
            WARN("TODO: Handle faults, for now just suspends the thread.\n");
            fault_print(msg, badge);
            rec_unschedule_current(cycle_count, rr_ChildState_Suspended);
        } break;
        case rr_IPCType_BlockChecker: {
            // if time did not progress, mark currently scheduled as blocked by recv.
            // less than 3 because unsuspending takes 2 cycles for some reason?

            // guaranteed to subtract and have a >= 0 value
            // this might be false if cycle_count overflows.
            // That is a problem for later.
            assert(cycle_count >= last_cycle_count);
            if (cycle_count - last_cycle_count < 3) {
                LOG("Cycle count did not increase, marking child %lu as \"BlockedOnRecv\"\n", rr_currently_sched->id);
                // Also check if we were expecting a reply.
                // If we were, then this is the case where the reply's IPC must be cancelled.
                if (rr_recv_source_channel != UNSET_VALUE) {
                    seL4_Word child_id = rr_channel_to_target_child_id[rr_recv_source_channel];
                    assert(child_id < rr_children_num);
                    WARN("Unexecuted reply object for child %lu! Suspending child %lu\n", child_id, child_id);

                    rr_children_arr[child_id].sched_state = rr_ChildState_Suspended;
                    rr_storage_store_scheduler_event(&_rr_storage_handle, cycle_count, child_id, rr_ChildState_Suspended);
                }
                rec_unschedule_current(cycle_count, rr_ChildState_BlockedOnRecv);
            }
            // Otherwise set it as schedulable.
            else {
                rec_unschedule_current(cycle_count, rr_ChildState_Schedulable);
            }
        } break;
        case rr_IPCType_Msg: {
            // This is a stub, and won't happen in the microkit context.
            assert(!"Unreachable");
        } break;
        case rr_IPCType_Call: {
            // if type is call, mark currently scheduled as blocked by call.
            // Messages can only come from calls (in microkit)
            // So we assume that we will receive a reply to this message eventually.

            // check if we did a nested ppcall, and stop (unsupported).
            if (rr_last_sched_child != NULL && rr_last_sched_child->sched_state == rr_ChildState_BlockedOnReply) {
                assert(!"Nested ppcalls are not supported through RR");
            }
            sending_child = rr_currently_sched->id;
            rec_unschedule_current(cycle_count, rr_ChildState_BlockedOnCall);
            assert(sending_child != NO_CHILD);

            // store the message in the target's recv queue.
            // Oopsie i need to be able to map a target channel to a child.
            seL4_Word source_ch = badge & CHANNEL_MASK;
            LOG("Source channel: %lu\n", source_ch);

            assert(source_ch < rr_channels_num);
            seL4_Word target_child = rr_channel_to_target_child_id[source_ch];
            LOG("Target child: %lu\n", target_child);

            rr_ipc_store_ipc_msg(target_child, msg, badge, source_ch);
            rr_storage_store_ipc_msg(&_rr_storage_handle, cycle_count, sending_child, badge, msg);
        } break;
        case rr_IPCType_Ntfn: {
            seL4_Word source_ch = rr_badge_to_channel_id(badge);
            // if the source_ch is too large, then we ignore it.
            // It probably came from the serial virtualiser or smth, but not sure why it's notifying us?
            if (source_ch >= rr_channels_num) {
                WARN("Extraneous channel of number %lu\n", source_ch);
                continue;
            }
            sending_child = rr_currently_sched->id;
            rec_unschedule_current(cycle_count, rr_ChildState_Schedulable);
            assert(sending_child != NO_CHILD);
            // store the message in the target's recv queue.
            // Oopsie i need to be able to map a target channel to a child.
            LOG("Source channel: %lu\n", source_ch);
            assert(source_ch < rr_channels_num);
            seL4_Word target_child = rr_channel_to_target_child_id[source_ch];
            LOG("Target child: %lu\n", target_child);
            rr_ipc_store_ipc_msg(target_child, msg, badge, source_ch);
            rr_storage_store_ipc_msg(&_rr_storage_handle, cycle_count, sending_child, badge, msg);
        } break;
        }
        // perform a reschedule.
        rec_perform_schedule(cycle_count);
        NO_ERR(seL4_TCB_Resume(rr_currently_sched->id + BASE_TCB_CAP));
        last_cycle_count = cycle_count;
    }
}

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

serial_queue_t *tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

// We never exit this.
void init()
{
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                  serial_config.tx.data.vaddr);
    // serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);
    LOG("INIT\n");

    rr_init(true);
    rec_main();
}

// Should not be called
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
}

// Should not be called
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}

// Should not be called.
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    *reply_msginfo = microkit_msginfo_new(0, 0);
    return false;
}
