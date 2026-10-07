#pragma once

#include "pager_config.h"

/* The benchmarks time themselves with these whether or not the pager is instrumented. */
static inline uint64_t read_cntpct(void)
{
    uint64_t value;

    asm volatile(
        "mrs %0, cntpct_el0"
        : "=r"(value)
    );

    return value;
}

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

#ifdef PAGER_INSTRUMENTATION

/* Faults each side has room to record. */
#define PAGER_MAX_SAMPLES 50000
/* The pager only records faults in the client's mmap arena, which is this long. */
#define PAGER_HEAP_SIZE 0x20000000UL
/* Label of the PPC a client sends to request instrumentation data. */
#define PAGER_INSTRUMENTATION_TAG 321

/*
 * When the benchmark took each fault and when it got control back. Defined by
 * the client, which sets them against what the pager recorded.
 */
extern uint64_t before_faults[PAGER_MAX_SAMPLES];
extern uint64_t after_faults[PAGER_MAX_SAMPLES];
/* Faults recorded so far. The pager and the benchmark each count their own. */
extern uint64_t fault_idx;

#endif
