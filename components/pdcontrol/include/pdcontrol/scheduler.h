#pragma once
#include <stddef.h>
#include "ipc_tracer.h"

typedef enum pdc_sched_pd_state {
    pdc_sched_pd_state_Schedulable = 0,
    pdc_sched_pd_state_Scheduled,
    pdc_sched_pd_state_BlockedOnSend,
    pdc_sched_pd_state_BlockedOnRecv,

    pdc_sched_pd_state_BlockedOnCall,  // Call -> Reply when the sending part succeeds.
    pdc_sched_pd_state_BlockedOnReply,

    pdc_sched_pd_state_Suspended,
    _pdc_sched_pd_state_ = 1ul << 63, // force to be seL4_Word size
} pdc_sched_pd_state_e;

typedef struct pdc_sched_pd_handle {
    const pdc_pd_config_t *cfg;
    pdc_sched_pd_state_e state;
} pdc_sched_pd_handle_t;

typedef struct pdc_sched_handle {
    const pdc_config_handle_t *cfg;
    pdc_sched_pd_handle_t *currently_sched;
    pdc_sched_pd_handle_t *last_sched;
    bool sched_next;

    pdc_sched_pd_handle_t sched_state_storage[PDC_MAX_CLIENTS];
    pdc_sched_pd_handle_t *sched_queue[PDC_MAX_CLIENTS];
} pdc_sched_handle_t;

pdc_sched_handle_t pdc_sched_init(pdc_config_handle_t *cfg);

// NULL gives the first pd
// if NULL is returned then we have run out of pd
// iterates in order of priority, and lru.
// returns a double pointer in order to also encode positional data in our
// scheduling queue.
pdc_sched_pd_handle_t **pdc_sched_iterate(pdc_sched_handle_t *handle, pdc_sched_pd_handle_t **cur);

// Schedules the child to be run when this calling PD stops.
pdc_sched_pd_handle_t *pdc_sched_schedule_child(pdc_sched_handle_t *handle, pdc_sched_pd_handle_t *child);

// Stops a pd from running when a pd is stopped.
pdc_sched_pd_handle_t *pdc_sched_unschedule_current(pdc_sched_handle_t *handle, pdc_sched_pd_state_e state);

// Does not schedule the child, rather just moves it to the back of the queue.
pdc_sched_pd_handle_t *pdc_sched_choose_child(pdc_sched_handle_t *handle, pdc_sched_pd_handle_t **cur);

void pdc_sched_setup_block_checker(pdc_sched_handle_t *handle);
bool pdc_sched_state_is_blocking(pdc_sched_pd_state_e state);

pdc_sched_pd_handle_t *pdc_sched_get_current(pdc_sched_handle_t *handle);
