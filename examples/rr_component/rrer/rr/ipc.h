#pragma once
#include "base.h"
#include "interfaces/sel4_client.h"
#include "microkit.h"
#include "sel4/shared_types_gen.h"
#include "types.h"
static inline void rr_init_ipc();
static inline rr_IPCType_e rr_ipc_get_type(seL4_MessageInfo_t msg, seL4_Word badge);
static inline microkit_child rr_ipc_get_child(seL4_Word badge);
static inline microkit_channel rr_ch_to_target(microkit_channel ch);

static inline microkit_channel rr_ch_to_target(microkit_channel ch)
{
    // we should never end up having channels equal to sender...
    assert(ch < sender_ch);
    if (ch % 2 == 0)
        return ch + 1;
    return ch - 1;
}

extern uint8_t *per_thread_recv_queue_mem;
extern seL4_Word per_thread_recv_queue_size;
rrer_queue_t *pt_recv_queue = NULL;
seL4_Word *rr_ipc_target_child_id = NULL;
static inline void rr_init_ipc()
{
    // just do naively for now.
    // reserve the first 8 bytes for the currently scheduled child.
    rr_ipc_target_child_id = (seL4_Word *)(per_thread_recv_queue_mem);
    *rr_ipc_target_child_id = NO_THREAD_SCHEDULED;
    pt_recv_queue = (rrer_queue_t *)(per_thread_recv_queue_mem + sizeof(seL4_Word));
    for (int i = 0; i * sizeof(rrer_queue_t) < per_thread_recv_queue_size; i++) {
        rrer_queue_init(per_thread_recv_queue_mem);
    }
}

static inline rr_IPCType_e rr_ipc_get_type(seL4_MessageInfo_t msg, seL4_Word badge)
{
    seL4_Word is_endpoint = badge >> BADGE_ENDPOINT_BIT;
    seL4_Word is_fault = (badge >> BADGE_FAULT_BIT) & 1;

    if (is_endpoint) {
        if ((badge & PD_MASK) == sender_ch) {
            // We received a reply
            return rr_IPCType_SenderReply;
        } else { // or not.
            return rr_IPCType_Call;
        }
    };
    if (is_fault)
        return rr_IPCType_Fault;

    seL4_Word idx = rr_badge_to_channel_id(badge);
    LOG("idx: %lu\n", idx);
    if (idx == blocker_ch) {
        return rr_IPCType_BlockChecker;
    }
    return rr_IPCType_Ntfn;
}

static inline microkit_child rr_ipc_get_child(seL4_Word badge)
{
    // This might only be valid for messages?
    return badge & PD_MASK;
}

// setup the ipc sender. The child starts from 0, using microkit_child-like semantics.
// There might be a race case... not sure tho.
static inline void rr_ipc_sender_setup(seL4_Word child)
{
    *rr_ipc_target_child_id = child;

    // notify the sender.
    microkit_notify(sender_ch);
}

// store the curretly stored message in the ipc buffer into the recv queue of that
// child.
static inline void rr_ipc_store_ipc_msg(seL4_Word target_child, seL4_MessageInfo_t msg, seL4_Word badge,
                                        seL4_Word target_ch)
{
    LOG("Storing message in child queue: %lu\n", target_child);
    assert(target_child < rr_children_num);
    rrer_queue_push(&pt_recv_queue[target_child], msg, badge, target_ch);
}

// check the size of the child's ipc queue.
static inline seL4_Word rr_ipc_child_queue_len(seL4_Word target_child)
{
    assert(target_child < rr_children_num);
    return rrer_queue_len(&pt_recv_queue[target_child]);
}

// Get the badge of the earliest sender.
static inline seL4_Word rr_ipc_child_queue_peek_badge(seL4_Word target_child)
{
    assert(target_child < rr_children_num);
    return rrer_queue_peek_badge(&pt_recv_queue[target_child]);
}

// Get the badge of the channel sender.
static inline seL4_Word rr_ipc_child_queue_peek_channel(seL4_Word target_child)
{
    assert(target_child < rr_children_num);
    return rrer_queue_peek_channel(&pt_recv_queue[target_child]);
}
