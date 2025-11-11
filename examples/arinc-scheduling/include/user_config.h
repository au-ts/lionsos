#pragma once

#include <stdint.h>
#include <scheduler_config.h>

// The metaprogram will omit a binary with the same format as this struct,
// and will be patched into the scheduler at system build time

typedef struct user_schedule {
    uint64_t timeslices[MAX_PARTITIONS];
    // @kwinter: In the future, will also need to keep track of
    // all the PD's in a partition so that we can easily suspend them
    uint32_t timeslice_ch[MAX_PARTITIONS];
    uint32_t num_timeslices;
} user_schedule_t;