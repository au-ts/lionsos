#pragma once

#include <microkit.h>

// The max partitions is limited by the number of channels that we can establish
// between the scheduler and a partition's initial process in microkit.
// One channel is taken by the sDDF timer subsystem.
#define MAX_PARTITIONS (MICROKIT_MAX_CHANNELS - 1)

typedef struct partition_config {
    uint32_t initial_pd_ch;
    // This can be extended in the future to contain more information
    // about each partition, such as the channel to it's error handler etc...
} partition_config_t;

typedef struct schedule {
    // Currently the timeslices are defined in nanoseconds
    uint32_t num_timeslices;
    uint64_t timeslices[MAX_PARTITIONS];
    uint32_t partition_id[MAX_PARTITIONS];
} schedule_t;

typedef struct scheduler_config {
    uint32_t num_partitions;
    partition_config_t partitions[MAX_PARTITIONS];
    schedule_t static_schedule;
} scheduler_config_t;
