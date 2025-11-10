#include <stdint.h>
#include <stdbool.h>

#include <microkit.h>
#include <sel4/sel4.h>
#include <os/sddf.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>

#include <scheduler_config.h>

/* Number of nanoseconds in a second */
#define NS_IN_S  1000000000ULL

__attribute__((__section__(".timer_client_config"))) timer_client_config_t config;

// @kwinter: This should be patched in
schedule_config_t schedule;

uint32_t current_timeslice;

// Bitstring for partition ready status. 0 = not ready, 1 = ready.
uint64_t part_ready;
uint64_t part_ready_check;

bool scheduler_running;

void next_partition() {
    current_timeslice++;

    // Wrap the schedule back around to the beginning if we are at the end
    if (current_timeslice == schedule.num_partitions) {
        current_timeslice = 0;
    }
    microkit_notify(current_timeslice);
    // Set a timeout for the length of this partition's timeslice
    sddf_timer_set_timeout(config.driver_id, schedule.timeslices[current_timeslice]);
}

void notified(microkit_channel ch)
{
    if (ch == config.driver_id) {
        if (scheduler_running == false) {
            microkit_notify(current_timeslice);
            sddf_timer_set_timeout(config.driver_id, schedule.timeslices[current_timeslice]);
            scheduler_running = true;
        } else {
            next_partition();
        }
    } else if (ch < schedule.num_partitions) {
        // This should be where all our partition channels are
        if ((part_ready & (1 << ch)) == 0) {
            sddf_dprintf("SCHEDULER | Marking partition %d as ready\n", ch);
            part_ready |= (1 << ch);
            // Check if all partitions are now initialised
            if (part_ready == part_ready_check) {
                // Now we return to our programmed schedule
                sddf_dprintf("SCHEDULER | All partitions ready, beginning schedule\n");
                // Timeout to let the last spd to become passive
                sddf_timer_set_timeout(config.driver_id, NS_IN_S);
            }
        }
    } else {
        sddf_dprintf("SCHEDULER |received unknown notification on channel: %d\n", ch);
    }
}

void init(void)
{
    // Setup partition schedule. We want a better way to configure this in the future
    schedule.num_partitions = 3;
    schedule.timeslices[0] = NS_IN_S;
    schedule.timeslices[1] = NS_IN_S;
    schedule.timeslices[2] = NS_IN_S;

    current_timeslice = 0;

    scheduler_running = false;

    assert(schedule.num_partitions <= MAX_PARTITIONS);

    // Construct the partition ready check value
    for (int i = 0; i < schedule.num_partitions; i++) {
        part_ready_check = (part_ready_check << 1) | 1;
    }
}

