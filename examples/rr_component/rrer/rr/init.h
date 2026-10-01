#pragma once
#include "base.h"
#include "scheduler.h"
#include "storage_backend/block.h"

static rr_storage_handle_t _rr_storage_handle = {0};

// The index is the id. Points to mr_prefilled data.
static inline void rr_init(bool is_recorder)
{
    assert(blocker_ch != UNSET_VALUE);
    assert(sender_ch != UNSET_VALUE);
    INFO("blocker_ch: %lu, sender_ch: %lu\n", blocker_ch, sender_ch);

	// TODO! Some of this stuff (if not all) should be converted into config structs.
    uint8_t *head = children_data_mem;
    assert(head != NULL);
    // initialises the children.
    rr_children_num = *head;
    head += sizeof(seL4_Word);

    rr_children_arr = (rr_Child_t *)head;
    head += sizeof(rr_Child_t) * rr_children_num;
    INFO("Num children: %lu @ %p\n", rr_children_num, rr_children_arr);

    // serialise channels
    rr_channels_num = *head;
    head += sizeof(seL4_Word);

    // serialise channel to target child
    rr_channel_to_target_child_id = (seL4_Word *)head;
    head += sizeof(seL4_Word) * rr_channels_num;
    INFO("Channel id map: %lu @ %p\n", rr_channels_num, rr_channel_to_target_child_id);
    for (int i = 0; i < rr_channels_num; i++) {
        INFO("Channel %d = child %lu\n", i, rr_channel_to_target_child_id[i]);
    }

    // initiaise scheduler queue
    rr_children_sched_queue = (rr_Child_t **)head;
    INFO("Scheduler queue @ %p\n", rr_children_sched_queue);

    rr_init_scheduler();
    rr_init_ipc();

    // initialise the vpmu to be recording and on.
    seL4_ARM_VPMU_VPMUNumCounters_t counters = seL4_ARM_VPMU_VPMUNumCounters(VPMU_CAP);
    NO_ERR(counters.error);
    INFO("Num pmu counters: %lu\n", counters.num_counters);
    assert(counters.num_counters > 0);
    NO_ERR(seL4_ARM_VPMU_VPMUCounterControl(VPMU_CAP, 1));

	if (is_recorder) _rr_storage_handle = rr_storage_start();
	else _rr_storage_handle = rr_storage_retrieve();
}
