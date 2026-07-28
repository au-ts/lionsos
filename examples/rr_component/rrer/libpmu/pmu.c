#include <microkit.h>
#include <stdbool.h>
#include "pmu.h"
#include "util.h"
#include "arm_pmu.h"
/* Number of nanoseconds in a millisecond */
#define NS_IN_MS 1000000ULL

/* Our internal representation of event nodes */
typedef struct libpmu_event {
    uint32_t event;
    uint64_t counter;
    /* This is the value last recorded when this event was scheduled. */
    uint32_t last_count;
    /* TODO: This is the period of the event. 0 means no period, anything else
    means MAX - period. */
    uint32_t period;

    /* Mark this as a currently valid entry */
    bool valid;
    /* Mark this as currently running on the hardware counters */
    bool in_hw;
    int hw_id;
    /* @kwinter: Extend with extra heuristics needed for efficient
    scheduling of event counters. */
} libpmu_event_t;

/* @kwinter: Can probably get rid of this and use an int array.
Doing this for now in case we need to store any extra metadata. */
typedef struct hw_counter {
    int event_id;
    bool used;
} hw_counter_t;

typedef struct libpmu_state {
    seL4_Word pmu_cap_idx;
    int num_events;
    bool libpmu_running;
    /* We will always place the cycle counter at the end */
    libpmu_event_t pmu_events[LIB_PMU_MAX_OPEN_EVENTS + 1];
    hw_counter_t hw_event_map[MAX_HW_COUNTERS];

    uint32_t start_index;
    uint32_t hyper_period;
    uint32_t curr_period;
    uint64_t sched_interval;
} libpmu_state_t;

libpmu_state_t state;

/* -------------- WRAPPERS AROUND RAW SEL4 PMU ACCESS CONTROL CAP API -------------- */

/* Get number of hardware counters availabel for use */
static inline uint32_t libpmu_get_num_counters(seL4_Word pmu_cap, uint32_t counter)
{
    seL4_ARM_PMUControl_NumCounters_t ret = seL4_ARM_PMUControl_NumCounters(pmu_cap);
    if (!ret.error) {
        return ret.num_counters;
    } else {
        return ret.error;
    }
}

/* Read event counter */
static inline uint32_t libpmu_read_event_counter(seL4_Word pmu_cap, uint32_t counter)
{
    seL4_ARM_PMUControl_ReadEventCounter_t ret = seL4_ARM_PMUControl_ReadEventCounter(pmu_cap, counter);
    if (ret.error == seL4_NoError) {
        return (uint32_t) ret.counter_value;
    } else {
        return ret.error;
    }
}

/* Write event counter */
static inline seL4_Error libpmu_write_event_counter(seL4_Word pmu_cap, uint32_t counter, uint32_t cnt_val, uint32_t event)
{
    return seL4_ARM_PMUControl_WriteEventCounter(pmu_cap, counter, cnt_val, event);
}


/* Read the cycle counter */
static inline uint64_t libpmu_read_cycle_counter(seL4_Word pmu_cap)
{
    seL4_ARM_PMUControl_ReadCycleCounter_t ret = seL4_ARM_PMUControl_ReadCycleCounter(pmu_cap);
    if (ret.error == seL4_NoError) {
        return (uint64_t) ret.cycle_counter_value;
    } else {
        return (uint64_t) ret.cycle_counter_value;
    }
}

/* Write the cycle counter */
static inline seL4_Error libpmu_write_cycle_counter(seL4_Word pmu_cap, uint64_t counter_value)
{
    return seL4_ARM_PMUControl_WriteCycleCounter(pmu_cap, counter_value);
}

/* Start all PMU counters */
static inline seL4_Error libpmu_start_counters(seL4_Word pmu_cap)
{
    return seL4_ARM_PMUControl_CounterControl(pmu_cap, 1);
}

/* Stop all PMU counters */
static inline seL4_Error libpmu_stop_counters(seL4_Word pmu_cap)
{
    return seL4_ARM_PMUControl_CounterControl(pmu_cap, 0);
}

/* Read interrupt value */
static inline uint32_t libpmu_read_int_val(seL4_Word pmu_cap)
{
    seL4_ARM_PMUControl_ReadInterruptValue_t ret = seL4_ARM_PMUControl_ReadInterruptValue(pmu_cap);
    if (ret.error == seL4_NoError) {
        return ret.interrupt_val;
    } else {
        return ret.error;
    }
}

/* Write interrupt value */
static inline seL4_Error libpmu_write_int_val(seL4_Word pmu_cap, uint32_t int_val)
{
    return seL4_ARM_PMUControl_WriteInterruptValue(pmu_cap, int_val);
}

/* Write interrupt control */
static inline seL4_Error libpmu_write_int_ctrl(seL4_Word pmu_cap, uint32_t int_ctrl)
{
    return seL4_ARM_PMUControl_InterruptControl(pmu_cap, int_ctrl);
}

/* --------------------------------------------------------------------------------- */

/* Allocate an ID based on what event is active */
int alloc_event_id()
{
    for (int i = 0; i < LIB_PMU_MAX_OPEN_EVENTS; i++) {
        if (!state.pmu_events[i].valid) {
            return i;
        }
    }
    return -1;
}

int deschedule_event(int libpmu_id)
{
    int hw_id = state.pmu_events[libpmu_id].hw_id;
    if (hw_id < 0 || hw_id >= MAX_HW_COUNTERS) {
        return -1;
    }

    /* Save the current counter value into here. */
    uint32_t ret = libpmu_read_event_counter(state.pmu_cap_idx, hw_id);

    state.pmu_events[libpmu_id].counter = ret;

    /* Mark this as no longer being scheduled, but don't actually remove it from the
    hardware counters for now, we will handle it during the next scheduled timeout.
    
    @kwinter: TODO - improve this, actually schedule something here.
    */
    state.hw_event_map[hw_id].used = false;
    state.hw_event_map[hw_id].event_id = 0;
    state.pmu_events[libpmu_id].hw_id = 0;
    return 1;
}

int libpmu_init(seL4_Word pmu_cap, uint64_t sched_interval)
{
    state.pmu_cap_idx = pmu_cap;
    /* Convert to nanoseconds */
    state.sched_interval = sched_interval * NS_IN_MS;
    return 1;
}

int libpmu_create_event_list(libpmu_event_node_t *hw_event_ids, int size)
{
    if (size < 0 || size >= MAX_HW_COUNTERS) {
        return -1;
    }

    if (state.libpmu_running) {
        return -2;
    }

    /* @kwinter: Need to do some bounds checking here or something */
    for (int i = 0; i < size; i++) {
        state.pmu_events[i].event = hw_event_ids[i].event;
        state.pmu_events[i].period = hw_event_ids[i].period;
        state.pmu_events[i].valid = true;
        state.num_events++;
    }

    return 1;
}

int libpmu_add_event_to_list(libpmu_event_node_t hw_event)
{
    if (state.num_events >= LIB_PMU_MAX_OPEN_EVENTS) {
        return -1;
    }

    if (state.libpmu_running) {
        return -2;
    }

    state.pmu_events[state.num_events].event = hw_event.event;
    state.pmu_events[state.num_events].period = hw_event.period;
    state.pmu_events[state.num_events].valid = true;

    state.num_events++;

    return 1;
}

int libpmu_clear_event_list(int libpmu_id)
{
    if (state.libpmu_running) {
        return -2;
    }

    for (int i = 0; i < state.num_events; i++) {
        state.pmu_events[i].valid = false;
        state.pmu_events[i].in_hw = false;
    }

    state.num_events = 0;

    return 1;
}

uint64_t libpmu_start_count(int *ret)
{
    /* Here we will create the static schedule for the counters. */

    libpmu_write_cycle_counter(state.pmu_cap_idx, 0);
    /* 1. Calculate the hyper-period, the total sum of periods in this schedule */
    state.hyper_period = (state.num_events / MAX_HW_COUNTERS) + 1;
    state.curr_period = 0;

    /* We will now set the state to running, and call schedule for the first period, then start the PMU */
    state.libpmu_running = true;
    int err;
    uint64_t timeout = libpmu_schedule(&err);
    if (err) {
        return 0;
    }

    libpmu_start_counters(state.pmu_cap_idx);
    return timeout;
}

int libpmu_stop_count(uint64_t *return_values, int size)
{
    libpmu_stop_counters(state.pmu_cap_idx);

    if (size < state.num_events) {
        return -1;
    }

    /* First interpolate all events in periods before the current period. */
    for (int i = 0; i < MAX_HW_COUNTERS; i++) {
        if (!state.hw_event_map[i].used) {
            break;
        }
        state.hw_event_map[i].used = false;

        state.pmu_events[state.start_index + i].counter = libpmu_read_event_counter(state.pmu_cap_idx, i);
    }

    /* Write all calculated totals out to the return values array */
    for (int i = 0; i < state.num_events; i++) {
        // TODO: Wrap this around platform specific ifdef. The cycle
        // counter might not always be a seperate counter on all
        // architectures.
        if (state.pmu_events[i].event == CYCLE_COUNTER) {
            return_values[i] = libpmu_read_cycle_counter(state.pmu_cap_idx);
            continue;
        }
        return_values[i] = state.pmu_events[i].counter;
    }

    return 1;
}

uint64_t libpmu_schedule(int *ret)
{
    /*
    This is where we will implement the policy for the multiplexing of hardware counters.

    For now, every one second we will schedule new events (n) for each counter if
    n >= MAX_HW_COUNTERS * 2. Otherwise, we will replace place at events in at random
    into the counters to ensure fairness (rather than just linerarly inserting, as the last
    counter would never be replace in this case)
     */
   
    /* Check if we have concluded the hyper period. If so, then restart from the first period */
    if (state.curr_period > state.hyper_period) {
        state.curr_period = 0;
    }


    /* 1. We must read all event counters, and record their values in the appropriate pmu_events entry. */
    for (int i = 0; i < MAX_HW_COUNTERS; i++) {
        if (!state.hw_event_map[i].used) {
            break;
        }
        if (state.pmu_events[state.start_index + i].event == CYCLE_COUNTER) {
            continue;
        }
        /* Record these values into the old start index from the last schedule */
        state.pmu_events[state.start_index + i].counter = libpmu_read_event_counter(state.pmu_cap_idx, i);
        state.hw_event_map[i].used = false;
    }

    state.start_index = state.curr_period * MAX_HW_COUNTERS;

    for (int i = 0; i < MAX_HW_COUNTERS; i++) {
        int curr_idx = state.start_index + i;
        if ((curr_idx) >= state.num_events) {
            break;
        }
        if (state.pmu_events[curr_idx].event == CYCLE_COUNTER) {
            continue;
        }
        /* 2. We must then interpolate the running counter for the events we are about to schedule.
        We will do this linearly, assume the last count is what would've occured over all remaining periods. */
        state.pmu_events[curr_idx].counter += state.pmu_events[curr_idx].last_count * (state.hyper_period - 1);


        /* 3. Schedule all events that are currently running. */
        libpmu_write_event_counter(state.pmu_cap_idx, i, 0, state.pmu_events[state.start_index + i].event);
        state.hw_event_map[i].event_id = state.pmu_events[state.start_index + i].event;
        state.hw_event_map[i].used = true;
    }

    state.curr_period++;

    return state.sched_interval;
}
