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
queue_t *pt_recv_queue = NULL;
seL4_Word* curr_sched_child = NULL;
static inline void rr_init_ipc()
{
    // just do naively for now.
    // reserve the first 8 bytes for the currently scheduled child.
    curr_sched_child = (seL4_Word *)(per_thread_recv_queue_mem);
    curr_sched_child[0] = -1;
    pt_recv_queue = (queue_t *)(per_thread_recv_queue_mem + sizeof(seL4_Word));
    for (int i = 0; i * sizeof(queue_t) < per_thread_recv_queue_size; i++) {
        queue_init(per_thread_recv_queue_mem);
    }
}

static inline rr_IPCType_e rr_ipc_get_type(seL4_MessageInfo_t msg, seL4_Word badge)
{
    return rr_IPCType_BlockChecker;
}

static inline microkit_child rr_ipc_get_child(seL4_Word badge) {
    return badge & PD_MASK;
}

// setup the ipc sender. The child starts from 0, using microkit_child-like semantics.
// There might be a race case... not sure tho.
static inline void rr_ipc_sender_setup(seL4_Word child) {
    curr_sched_child[0] = child;
    // restart the sender.
    NO_ERR(seL4_TCB_Suspend(TCB(child)));
    microkit_pd_restart(child, DEFAULT_ENTRY_POINT);
    NO_ERR(seL4_TCB_Resume(TCB(child)));
}
