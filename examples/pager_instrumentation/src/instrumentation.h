#pragma once
#include <stdint.h>

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

static inline uint64_t ticks_to_ns(uint64_t ticks, uint64_t freq)
{
    return (ticks * 1000000000ULL) / freq;
}
