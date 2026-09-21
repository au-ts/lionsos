#pragma once

// Do not include this file directly.
#include "sel4/functions.h"
#include "sel4/shared_types_gen.h"
#include <sddf/util/printf.h>
#include "../base.h"
#include "../ipc.h"

#define RR_STORAGE_HANDLE_STORAGE_SIZE 1024
#define RR_STORAGE_MAGIC "potato"
#define RR_MAX_CHILDREN 32
// A handle to the storage unit.

// We treat the storage backend similarly to a elf file.
// It two main sections - one for storing variable-sized data (like .data),
// and the other for the instructions.
typedef struct rr_storage_handle {
    uint8_t magic[sizeof(RR_STORAGE_MAGIC)];
    // The text section holds data in the given shape
    // [cycle count] [unit type] [specific unit args]
    // Each part is word size, with total size being sizeof(rr_storage_unit_t)
    // References to any stored data will be relaive to data_offset
    seL4_Word num_children;
    seL4_Word children_prio[RR_MAX_CHILDREN];
    seL4_Word text_offset;
    seL4_Word text_size;
    seL4_Word data_offset;
    seL4_Word data_size;
    // Possibly add more metadata to ensure that the number of children are the same?
    // And that their ids are the same?
    uint8_t msg_read_storage[RR_STORAGE_HANDLE_STORAGE_SIZE];
    uint8_t msg_write_storage[RR_STORAGE_HANDLE_STORAGE_SIZE];
} rr_storage_handle_t;


typedef enum rr_storage_unit_type {
    rr_storage_unit_REPLY = 1,
    rr_storage_unit_MSG, // no msgs
    rr_storage_unit_CALL, 
    rr_storage_unit_NTFN,
    rr_storage_unit_SCHED, 
    _rr_storage_unit_max = 1 << 63,
} rr_storage_unit_type_e;

// The backing type for replies and calls.
typedef struct rr_storage_msg_args {
    seL4_Word source_chan; // We can determine the source and target children from here.
    seL4_Word target_chan;
    seL4_Word label;
    seL4_Word badge;
    seL4_Word data_offset;
} rr_storage_msg_args_t;

// The backing type for scheduling events
typedef struct rr_storage_sched_args {
    seL4_Word child;
    rr_ChildState_e state; // also word-sized.
} rr_storage_sched_args_t;

typedef struct rr_storage_unit {
    seL4_Word cycle_count;
    rr_storage_unit_type_e unit_type;
    seL4_Word args[6];
} rr_storage_unit_t;

// satisfied by included backend. (which this file is included by).
static inline void rr_init_storage_backend();

static inline void rr_storage_store_ipc_backend(seL4_Word cycle_count, seL4_Word source_child, seL4_Word badge,
                                           seL4_MessageInfo_t msg);
static inline void rr_storage_store_scheduler_event_backend(seL4_Word cycle_count, seL4_Word child_id, rr_ChildState_e new_state);

static inline void rr_init_storage()
{
    REC("init\n");
    rr_init_storage_backend();
}

// We store ipc messages
static inline void rr_storage_store_ipc_msg(seL4_Word cycle_count, seL4_Word source_child, seL4_Word badge,
                                           seL4_MessageInfo_t msg)
{
    switch (rr_ipc_get_type(msg, badge)) {
    case rr_IPCType_BlockChecker: {
        assert(false);
    } break;
    case rr_IPCType_SenderReply: {
        // Format: [cycle_count] Reply [source_child] [target_child] [badge] [message]
        seL4_Word target_child = rr_channel_to_target_child_id[rrer_source_ch_to_target_ch(rr_recv_source_channel)];
        REC("0x%lx Reply child_%lu child_%lu %lx %lx", cycle_count, source_child, target_child, badge, msg.words[0]);
        seL4_Word msglen = seL4_MessageInfo_get_length(msg);
        for (seL4_Word i = 0; i < msglen; i++) {
            sddf_printf(" %lx", seL4_GetMR(i));
        }
        sddf_printf("\n");
    } break;
    case rr_IPCType_Msg: {
        assert(false);
    } break;
    case rr_IPCType_Fault: {
        assert(!"TODO: handle faults");
    } break;
    case rr_IPCType_Call: {
        // Format: [cycle_count] msg [source_child] [source_channel] [target_child] [target_channel] [badge] [message]
        // needs to be channel mask, the other is for ntfns.
        seL4_Word source_ch = badge & CHANNEL_MASK;
        // not sus
        seL4_Word target_child = rr_channel_to_target_child_id[source_ch];
        seL4_Word target_ch = rrer_source_ch_to_target_ch(source_ch);
        REC("0x%lx %s child_%lu channel_%lu child_%lu channel_%lu %lx %lx", cycle_count,
            rr_ipc_type_to_string(rr_IPCType_Call), source_child, source_ch, target_child, target_ch, badge,
            msg.words[0]);
        seL4_Word msglen = seL4_MessageInfo_get_length(msg);
        for (seL4_Word i = 0; i < msglen; i++) {
            sddf_printf(" %lx", seL4_GetMR(i));
        }
        sddf_printf("\n");
    } break;
    case rr_IPCType_Ntfn: {
        // only allowed for ntfns.
        seL4_Word source_ch = rr_badge_to_channel_id(badge);
        // not sus
        seL4_Word target_child = rr_channel_to_target_child_id[source_ch];
        seL4_Word target_ch = rrer_source_ch_to_target_ch(source_ch);
        REC("0x%lx %s child_%lu channel_%lu child_%lu channel_%lu %lx %lx\n", cycle_count,
            rr_ipc_type_to_string(rr_IPCType_Ntfn), source_child, source_ch, target_child, target_ch, badge,
            msg.words[0]);
    } break;
    }
    rr_storage_store_ipc_backend(cycle_count, source_child, badge, msg);
}

// We store scheduler events
static inline void rr_storage_store_scheduler_event(seL4_Word cycle_count, seL4_Word child_id, rr_ChildState_e new_state)
{
    REC("0x%lx scheduler child_%lu %s\n", cycle_count, child_id, rr_child_state_to_string(new_state));
    rr_storage_store_scheduler_event_backend(cycle_count, child_id, new_state);
}
