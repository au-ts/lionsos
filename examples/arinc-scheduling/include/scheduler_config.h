#pragma once

#include <microkit.h>

// @kwinter: This is a first iteration of this config file. We will move it to the
// metaprogram after

// The max partitions is limited by the number of channels that we can establish
// between the scheduler and a partition's initial process in microkit.
// One channel is taken by the sDDF timer subsystem.
#define MAX_PARTITIONS (MICROKIT_MAX_CHANNELS - 1)

typedef struct schedule_config {
    uint32_t num_partitions;
    // Currently this is in NS
    uint64_t timeslices[MAX_PARTITIONS];
    microkit_channel partition_initial_pd[MAX_PARTITIONS];
} schedule_config_t;
