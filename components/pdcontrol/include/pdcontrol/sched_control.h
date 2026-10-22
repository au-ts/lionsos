#pragma once

#include "config.h"
#include "pdcontrol.h"

typedef enum {
    pdc_pd_sched_state_Invalid,
    pdc_pd_sched_state_Schedulable,
    pdc_pd_sched_state_Scheduled,
    // no send, because assuming pure microkit context.
    // pdc_pd_sched_state_BlockedOnSend,
    pdc_pd_sched_state_BlockedOnRecv,

    pdc_pd_sched_state_BlockedOnCall,  // Call -> Reply when the sending part succeeds.
    pdc_pd_sched_state_BlockedOnReply,

    pdc_pd_sched_state_Suspended,
    _pdc_pd_sched_state_ = 1ul << 63, // force to be seL4_Word size
} pdc_pd_sched_state_e;

typedef struct {
    const pdc_pd_config_t* cfg;
    seL4_Word cur_prio;
    pdc_pd_sched_state_e sched_state;
} pdc_pd_sched_state_t;

typedef struct {
    const pdc_config_handle_t* cfg;
    seL4_Word num_clients;
    // map from id to value.
    pdc_pd_sched_state_t data[PDC_MAX_CLIENTS];
    pdc_pd_sched_state_t* curr_sched;
    pdc_pd_sched_state_t* last_sched;
} pdc_sched_handle_t;

// sctrl -> Schedule controller
pdc_sched_handle_t pdc_sctrl_init(pdc_config_handle_t* cfg);

// Changes the schedule of a specific thread.
void pdc_sctrl_change_state(pdc_sched_handle_t* pdc, seL4_Word pd_id, pdc_pd_sched_state_e new_state);

// Performs the final hooks required to schedule the thread.
void pdc_sctrl_schedule();
