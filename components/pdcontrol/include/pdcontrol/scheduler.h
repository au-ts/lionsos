/**
 * Custom userspace "scheduler".
 *
 * Allows us to keep track of the state of PDs by manually scheduling them.
 * It is not possible to my knowledge to get the current scheduling state of TCBs from seL4.
 *
 * A basic tiered round robin scheduler which is similar? to the one implemented by the seL4?
 * Just keeps track of multiple queues of pds to schedule, sorted by priority then by LRU.
 *
 * DOES NOT automatically update states, nor can deduce which state a PD should be in.
 *  (It is possible to emperically determine if a child PD is blocked by send/ppcall, although unideal.)
 * DOES NOT do preemption. Use another PD to wake up the scheduler.
 *
 * The main method for preemption is the following:
 * - Separate PD X at some higher priority. Make scheduling a PD change the priority of said PD to be
 *   equivalent to this separate PD X
 * - Make PD X perform a yield, and then a notification to the scheduler
 *   This ensures that when the currently scheduled PD is blocked or runs out of it's time slice,
 *   that we preempt the "scheduler".
 */

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

// Unimplemented
void pdc_sched_setup_block_checker(pdc_sched_handle_t *handle);

bool pdc_sched_state_is_blocking(pdc_sched_pd_state_e state);

pdc_sched_pd_handle_t *pdc_sched_get_current(pdc_sched_handle_t *handle);
