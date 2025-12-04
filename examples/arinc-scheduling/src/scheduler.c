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
__attribute__((__section__(".scheduler_config"))) scheduler_config_t scheduler_config;

uint32_t current_timeslice;

// Bitstring for partition ready status. 0 = not ready, 1 = ready.
uint64_t part_ready;
uint64_t part_ready_check;

bool scheduler_running;

void next_partition() {
    current_timeslice++;

    // Wrap the schedule back around to the beginning if we are at the end
    if (current_timeslice == scheduler_config.static_schedule.num_timeslices) {
        current_timeslice = 0;
    }

    uint32_t part_id = scheduler_config.static_schedule.partition_id[current_timeslice];
    microkit_notify(scheduler_config.partitions[part_id].initial_pd_ch);
    // Set a timeout for the length of this partition's timeslice
    sddf_timer_set_timeout(config.driver_id, scheduler_config.static_schedule.timeslices[current_timeslice]);
}

int part_id_from_ch(microkit_channel ch)
{
    for (int i = 0; i < scheduler_config.num_partitions; i++) {
        if (scheduler_config.partitions[i].initial_pd_ch == ch) {
            return i;
        }
    }
    return -1;
}

void notified(microkit_channel ch)
{
    if (ch == config.driver_id) {
        if (scheduler_running == false) {
            // In this case, this interrupt will be from the first timeout we set after all partition init
            // is complete. Here we will begin normal operation of the scheduler.
            uint32_t part_id = scheduler_config.static_schedule.partition_id[current_timeslice];
            microkit_notify(scheduler_config.partitions[part_id].initial_pd_ch);
            sddf_timer_set_timeout(config.driver_id, scheduler_config.static_schedule.timeslices[current_timeslice]);
            scheduler_running = true;
        } else {
            next_partition();
        }
    } else {
        // Find the partition id associated with the channel
        int part_id = part_id_from_ch(ch);
        if (part_id == -1) {
            sddf_dprintf("SCHEDULER |received unknown notification on channel: %d\n", ch);
        } else {
            if ((part_ready & (1 << ch)) == 0) {
                sddf_dprintf("SCHEDULER | Marking partition %d as ready\n", part_id);
                part_ready |= (1 << part_id);
                // Check if all partitions are now initialised
                if (part_ready == part_ready_check) {
                    // Now we return to our programmed schedule
                    sddf_dprintf("SCHEDULER | All partitions ready, beginning schedule\n");
                    // Timeout to let the last spd become passive
                    sddf_timer_set_timeout(config.driver_id, NS_IN_S);
                }
            }
        }
    }
}

void init(void)
{
    current_timeslice = 0;

    scheduler_running = false;

    // Construct the partition ready check value
    for (int i = 0; i < scheduler_config.num_partitions; i++) {
        // Construct the ready check based on the channels to the partitions
        // initial task.
        part_ready_check |= (1 << scheduler_config.partitions[i].initial_pd_ch);
    }
}

