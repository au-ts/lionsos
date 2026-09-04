#pragma once

// The base, all includes and main types.
#include "sel4/simple_types.h"
#include "types.h"
#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>

#define LOG(...) do {sddf_printf("RRER [%s]| ", __func__); sddf_printf(__VA_ARGS__);} while (0)

#define VPMU_CAP BASE_VPMU_CAPS
#define NO_ERR(X) assert(X == seL4_NoError)
#define TCB(X) (X + BASE_TCB_CAP)
#define SELF_TCB() microkit_cspace_root_slot_to_cptr(1)
#define CHILD_SCHEDCTXT(child_id) microkit_cspace_root_slot_to_cptr(2 + child_id)
#define INPUT_CAP 1
#define REPLY_CAP 4
#define SENDER_ID 61
#define BLOCK_CHECKER_ID 60

#define SCHED_PRIO 250
#define UNSCHED_PRIO 1
#define SENDER_PRIO 251
#define SELF_PRIO 253
#define BLOCK_PRIO SCHED_PRIO

#define PD_MASK 0xff
#define BADGE_GET_PD_ID(badge) (badge & PD_MASK)
#define CHANNEL_MASK 0x3f

#define BADGE_FAULT_BIT 62
#define BADGE_ENDPOINT_BIT 63

#ifndef SENDER_ENTRY_POINT
#error define SENDER_ENTRY_POINT
#endif
#ifndef BLOCK_CHECKER_ENTRY_POINT
#error define BLOCK_CHECKER_ENTRY_POINT
#endif
#define NO_CHILD ((seL4_Word)-1)

#define RR_CHILDSTATE_ENUM(x) (0xfffffffful & (x))
#define RR_CHILDSTATE_VALUE(x) (((0xfffffffful << 32) & (x)) >> 32)
#define RR_CHILDSTATE_SET_VALUE(state, val) (((val) << 32) | state)
typedef enum {
    rr_ChildState_Schedulable = 0,
    rr_ChildState_Scheduled,
    rr_ChildState_BlockedOnSend, // the target is stored in the upper 32 bits.
    rr_ChildState_BlockedOnRecv,
    // We store any data relevant in the top 32 bits.
    _rr_ChildState_ = 1ul << 63, // force to be seL4_Word size
} rr_ChildState_e;

typedef enum {
    rr_SchedState_None = 0,
    rr_SchedState_Scheduled,
    rr_SchedState_Blocked,
} rr_SchedState_e;

typedef struct {
    seL4_Word id;
    seL4_Word priority;
    rr_ChildState_e sched_state;
} rr_Child_t;

typedef enum {
    rr_IPCType_BlockChecker,
    rr_IPCType_Ntfn, // irqs are ntfns from the perspective of IPC.
    rr_IPCType_Msg,
    // consider stuff about fault handlers.
} rr_IPCType_e;

typedef struct table_meta_data {
    uint64_t table_data_base;
    uint64_t pgd[64];
} table_metadata_t;

/* CHANNELS, MAPS, SETVARS */
seL4_Word sender_ch = UNSET_VALUE;
seL4_Word blocker_ch = UNSET_VALUE;

uint8_t *per_thread_recv_queue_mem = NULL;
seL4_Word per_thread_recv_queue_size = 0;

table_metadata_t table_metadata = { 0 };
uint8_t *children_data_mem = NULL;

/* STATE */
rr_Child_t *rr_children_arr = NULL;
seL4_Word rr_children_num = 0;

rr_Child_t *rr_currently_sched = NULL;
bool rr_scheduled_next = false;

rr_SchedState_e rr_sched_state = rr_SchedState_None;
rr_Child_t **rr_children_sched_queue = NULL;

seL4_Word rr_channels_num = 0;
seL4_Word *rr_channel_to_target_child_id = NULL;

static inline seL4_Word rr_badge_to_ch_id(seL4_Word badge)
{
    unsigned int idx = 0;
    do {
        if (badge & 1) {
            break;
        }
        badge >>= 1;
        idx++;
    } while (badge != 0);
    return idx;// __builtin_ctz(badge);
}

static inline const char *rr_child_state_to_string(rr_ChildState_e state)
{
    switch (state) {
    case rr_ChildState_Schedulable:
        return "Schedulable";
    case rr_ChildState_Scheduled:
        return "Scheduled";
    case rr_ChildState_BlockedOnSend:
        return "BlockedOnSend";
    case rr_ChildState_BlockedOnRecv:
        return "BlockedOnRecv";
    default:
        return "Unknown child state";
    }
}

static inline const char *rr_ipc_type_to_string(rr_IPCType_e type)
{
    switch (type) {
    case rr_IPCType_BlockChecker:
        return "BlockChecker";
    case rr_IPCType_Ntfn:
        return "Ntfn";
    case rr_IPCType_Msg:
        return "Msg";
    default:
        return "Unknown msg type";
    }
}
