#pragma once

#include <stdint.h>

// Label of the PPC a client sends to request instrumentation data.
#define PAGER_INSTRUMENTATION_TAG 321

// Uncomment this line to enable debug logging for the pager component
// #define PAGER_DEBUG

// Uncomment this line to enable instrumentation for the pager component
#define PAGER_INSTRUMENTATION


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
#endif