#include <stdint.h>
#include <stdbool.h>

#include <microkit.h>
#include <sel4/sel4.h>
#include <os/sddf.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>

#include <scheduler_config.h>
#include <user_config.h>

/* Number of nanoseconds in a second */
#define NS_IN_S  1000000000ULL

__attribute__((__section__(".timer_client_config"))) timer_client_config_t config;
__attribute__((__section__(".user_schedule"))) user_schedule_t user_schedule;

uint32_t current_timeslice;

// Bitstring for partition ready status. 0 = not ready, 1 = ready.
uint64_t part_ready;
uint64_t part_ready_check;

// This is calculated at the start of every schedule cycle. Once we have
// reached the end of the schedule, we will check sleep until the hyperperiod
// has ended.
uint64_t hyperperiod_end;

bool scheduler_running;

void next_partition() {
    current_timeslice++;

    // Wrap the schedule back around to the beginning if we are at the end
    if (current_timeslice >= user_schedule.num_timeslices) {
        // Check if we need to pad the rest of the hyperschedule
        uint64_t current_time = sddf_timer_time_now(config.driver_id);
        if (current_time < hyperperiod_end) {
            sddf_timer_set_timeout(config.driver_id, hyperperiod_end - current_time);
            return;
        } else {
            hyperperiod_end = current_time + user_schedule.hyperperiod;
            current_timeslice = 0;
        }
    }
    microkit_notify(current_timeslice);
    // Set a timeout for the length of this partition's timeslice
    sddf_timer_set_timeout(config.driver_id, user_schedule.timeslices[current_timeslice]);
}

void start_schedule() {
    uint64_t current_time = sddf_timer_time_now(config.driver_id);
    hyperperiod_end = current_time + user_schedule.hyperperiod;
    microkit_notify(current_timeslice);
    sddf_timer_set_timeout(config.driver_id, user_schedule.timeslices[current_timeslice]);
}

void notified(microkit_channel ch)
{
    if (ch == config.driver_id) {
        if (scheduler_running == false) {
            start_schedule();
            scheduler_running = true;
        } else {
            next_partition();
        }
    } else if (ch < user_schedule.num_timeslices) {
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
    current_timeslice = 0;

    scheduler_running = false;
    
    uint64_t total_partition_time = 0;

    // Construct the partition ready check value
    for (int i = 0; i < user_schedule.num_timeslices; i++) {
        // Construct the ready check based on the channels to the partitions
        // initial task.
        part_ready_check |= (1 << user_schedule.timeslice_ch[i]);
        total_partition_time += user_schedule.timeslices[i];
    }

    assert(user_schedule.hyperperiod >= total_partition_time);
}

