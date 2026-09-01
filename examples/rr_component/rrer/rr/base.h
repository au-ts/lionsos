#pragma once

// The base, all includes and main types.
#include "sel4/simple_types.h"
#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("RRER | " __VA_ARGS__)
#define VPMU_CAP BASE_VPMU_CAPS
#define NO_ERR(X) assert(X == seL4_NoError)
#define TCB(X) (X + BASE_TCB_CAP)
#define SELF_TCB() microkit_cspace_root_slot_to_cptr(1)
#define CHILD_SCHEDCTXT(child_id) microkit_cspace_root_slot_to_cptr(2 + child_id)
#define INPUT_CAP 1
#define REPLY_CAP 4
#define SENDER_ID 61

#define SCHED_PRIO 250
#define UNSCHED_PRIO 1
#define SENDER_PRIO 251
#define SELF_PRIO 253
#define BLOCK_PRIO SCHED_PRIO

typedef enum {
    rr_ChildState_Schedulable = 0,
    rr_ChildState_Scheduled,
    rr_ChildState_BlockedOnSend,
    rr_ChildState_BlockedOnRecv,
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
    rr_IPCType_Ntfn,
    rr_IPCType_Irq,
    rr_IPCType_Msg,
} rr_IPCType_e;

typedef struct table_meta_data {
    uint64_t table_data_base;
    uint64_t pgd[64];
} table_metadata_t;

/* CHANNELS, MAPS, SETVARS */
seL4_Word sender_ch = 0;
seL4_Word blocker_ch = 0;

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
