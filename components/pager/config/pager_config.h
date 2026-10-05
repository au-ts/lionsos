#pragma once

// Uncomment this line to enable debug logging for the pager component
// #define PAGER_DEBUG

// Uncomment this line to enable instrumentation for the pager component
// #define PAGER_INSTRUMENTATION

#ifdef PAGER_INSTRUMENTATION
uint64_t read_cntpct(void)
{
    uint64_t value;

    asm volatile(
        "mrs %0, cntpct_el0"
        : "=r"(value)
    );

    return value;
}
#endif