#pragma once

#include "pager_config.h"

#ifdef PAGER_INSTRUMENTATION
static inline uint64_t read_cntpct(void)
{
    uint64_t value;

    asm volatile(
        "mrs %0, cntpct_el0"
        : "=r"(value)
    );

    return value;
}

#define PAGER_MAX_SAMPLES 50000
#define PAGER_HEAP_SIZE 0x20000000UL
#define PAGER_INSTRUMENTATION_TAG 321

extern uint64_t before_faults[50000];
extern uint64_t after_faults[50000];
extern uint64_t fault_idx;

static inline uint64_t read_cntfrq(void)
{
    uint64_t value;

    asm volatile(
        "mrs %0, cntfrq_el0"
        : "=r"(value)
    );

    return value;
}

// I need this to be long double in order to have accurate measurements.
static inline long double ticks_to_ns(long double ticks, uint64_t freq)
{
    return (ticks * 1000000000) / freq;
}

#endif