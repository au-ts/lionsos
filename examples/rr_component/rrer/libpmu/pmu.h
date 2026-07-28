/* Header file for a basic PMU access library, wrapping around the raw seL4
invocations to the PMU Access Control capability. */

/* TODO: Hide architecture in here. Wrap ARM and RISCV around ifdefs*/
#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

#define LIB_PMU_MAX_OPEN_EVENTS 32

/* @kwinter: Need a better way to embed this information */
#define MAX_HW_COUNTERS 6

/* @kwinter: Write an enum for the error types */

typedef struct libpmu_event_node {
    /* This event should match the HW event ID for
    the platform that this library is running on */
    uint32_t event;
    /* This is a period that you may want to sample on.
    libpmu will set the counter value to MAX - period.
    If you would not like a period to be set, please set
    this field to 0 */
    uint64_t period;
} libpmu_event_node_t;

/*
Initialise the state of the PMU access library.

Args:
pmu_cap: the Cptr to the PMU access control capability.
sched_interval: The time in ms for each period in the schedule.
The default interval is 10ms.
 */
int libpmu_init(seL4_Word pmu_cap, uint64_t sched_interval);

/*
Intialise the state of libpmu event list.

Returns -1 if supplied array is too big
Returns -2 if libpmu is currently running.
 */
int libpmu_create_event_list(libpmu_event_node_t *hw_event_ids, int size);

/*
Add an event to the internal libpmu event list.

Returns -1 if event list is already full.
Returns -2 if libpmu is currently running.
 */
int libpmu_add_event_to_list(libpmu_event_node_t hw_event);

/*
Clear the entire event list.

Returns -2 if libpmu is currently running.
 */
int libpmu_clear_event_list(int libpmu_id);

/*
Start libpmu with the event set that has already been defined.

On success, returns a timeout to set for the schedule period.
Returns -1 if event set is of size 0.
Returns -2 if libpmu is already running.
 */
uint64_t libpmu_start_count(int *ret);

/*
Stops libpmu. The counter values will then be written to the
supplied return value array.

Return -1 if size != libpmu's internal number of events.
 */
int libpmu_stop_count(uint64_t *return_values, int size);

/*
Invoke the libpmu internal scheduler. Call this from
the IRQ handler for a timer notification, and set the
next timer period to the return value of this function.
The return value will be in ns.
 */
uint64_t libpmu_schedule(int *ret);

/* Handle the overflow of a counter, call this function
from the IRQ handler for PMU overflows */
int libpmu_handle_overflow(uint32_t interrupt_flags);