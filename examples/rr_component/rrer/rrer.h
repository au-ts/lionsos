#include "interfaces/sel4_client.h"
#include "sel4/errors.h"
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "sel4/syscalls_mcs.h"
#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("RRER | " __VA_ARGS__)
#define VPMU_CAP BASE_VPMU_CAPS
#define NO_ERR(X) assert(X == seL4_NoError)
#define TCB(X) (X + BASE_TCB_CAP)
#define SELF_TCB() microkit_cspace_root_slot_to_cptr(1)
#define INPUT_CAP 1
#define REPLY_CAP 4

typedef enum {
    rr_ChildState_Schedulable = 0,
    rr_ChildState_Scheduled,
    rr_ChildState_Blocked,
    _rr_ChildState_ = 1 << 63, // force to be seL4_Word size
} rr_ChildState_e;

typedef enum {
    rr_SchedState_None = 0,
    rr_SchedState_Scheduled,
    rr_SchedState_Blocked,
} rr_SchedState_e;

typedef struct {
    seL4_Word id;
    seL4_Word priority;
    rr_ChildState_e sched_state;
} rr_Child_t;

static inline void rrer_main();
static inline void rrer_init(seL4_Word* data);
static inline void rrer_init_scheduler_();
static inline rr_Child_t* rrer_get_next_scheduled();
static inline void rrer_schedule(rr_Child_t* child);
static inline void rrer_unschedule();
static inline microkit_channel rrer_ch_to_target_(microkit_channel ch);
static inline void rrer_reschedule();

/* STATE */
rr_Child_t* children_arr = NULL;
rr_Child_t** children_sched_queue = NULL;
seL4_Word children_num = 0;
rr_Child_t* currently_sched = NULL;
bool scheduled_next = false;
rr_SchedState_e sched_state = rr_SchedState_None;

// The index is the id. Points to mr_prefilled data.
static inline void rrer_init(seL4_Word* data) {
    // initialises the children.
    children_num = data[0];
    children_arr = (rr_Child_t*)&data[1];
    children_sched_queue = (rr_Child_t**)&children_arr[children_num];
    LOG(
        "children_arr: %p, children_num: %lu, children_sched_queue: %p\n", 
        children_arr,
        children_num, 
        children_sched_queue
    );
    rrer_init_scheduler_();

    // initialise the vpmu to be recording and on.
    seL4_ARM_VPMU_VPMUNumCounters_t counters = seL4_ARM_VPMU_VPMUNumCounters(VPMU_CAP);
    NO_ERR(counters.error);
    assert(counters.num_counters > 0);
    NO_ERR(seL4_ARM_VPMU_VPMUCounterControl(VPMU_CAP, 1));
}

static inline void rrer_init_scheduler_() {
    // We do not unschedule the children, but they can never run because our priority is higher.
    // This ensures that a send to a "suspended" tcb is always received.
    // ASSUMPTION: NOT SMP KERNEL!!!
    // Populate the schedule queue (but not sort it)
    for (int i = 0; i < children_num; i++) {
        children_sched_queue[i] = &children_arr[i];
        LOG(
            "id: %lu, priority: %lu, sched_state: %lu\n", 
            children_sched_queue[i]->id,
            children_sched_queue[i]->priority,
            (seL4_Word)children_sched_queue[i]->sched_state
        );
    }

    // Sort the schedule queue.
    // funny bubble sort
    for (int up = children_num - 1; up >= 0; up--) {
        for (int i = 0; i < up - 1; i++) {
            if (children_sched_queue[i]->priority < children_sched_queue[i + 1]->priority)
            {
                rr_Child_t* temp = children_sched_queue[i];
                children_sched_queue[i] = children_sched_queue[i + 1];
                children_sched_queue[i + 1] = temp;
            }
        }
    }
}

static inline rr_Child_t* rrer_get_next_scheduled() {
    // choose the next highest priority, currently schedulable child
    // and then move it to the end of the queue for that specific priority level.
    // and sets that child to be scheduled.
    // Returns NULL if no one can be scheduled.
    rr_Child_t* child = NULL;

    for (int i = 0; i < children_num; i++) {
        if (children_sched_queue[i]->sched_state == rr_ChildState_Schedulable) {
            child = children_sched_queue[i];
            int j = i + 1;
            for (; 
                j < children_num && children_sched_queue[j]->priority == child->priority;
                j++) 
            {
                children_sched_queue[j-1] = children_sched_queue[j];
            }
            children_sched_queue[j-1] = child;
            LOG("Found child to schedule: id %lu\n", child->id);
            break;
        }
    }
    return child;
}

static inline seL4_Word rrer_cyclecount_diff_(seL4_Word old_value) {
    seL4_ARM_VPMU_VPMUReadCycleCounter_t vpmu_count = seL4_ARM_VPMU_VPMUReadCycleCounter(VPMU_CAP);
    NO_ERR(vpmu_count.error);

    int64_t diff = vpmu_count.cycle_counter_value - old_value;
    if (diff < 0) diff = -diff;

    // guaranteed to be positive.
    return (seL4_Word)diff;
}

static inline void rrer_schedule(rr_Child_t* child) {
    assert(child != NULL);
    // Suspend here so that we don't get preempted on priority setting.
    NO_ERR(seL4_TCB_Suspend(TCB(child->id)));
    child->sched_state = rr_ChildState_Scheduled;

    // vpmu is guaranteed not to be bound.
    NO_ERR(seL4_TCB_BindVPMU(TCB(child->id), VPMU_CAP));

    // need to set it's priority to be as high as ours so that it can be scheduled.
    // has to be the same; if higher then we cannot process non blocking IPC.
    // (although we could probably never?)
    NO_ERR(seL4_TCB_SetPriority(TCB(child->id), SELF_TCB(), 254));

    // Cannot resume here because it might preempt us.
    scheduled_next = true;
    sched_state = rr_SchedState_Scheduled;
    currently_sched = child;
}

static inline void rrer_unschedule() {
    assert(currently_sched != NULL);
    NO_ERR(seL4_TCB_SetPriority(TCB(currently_sched->id), SELF_TCB(), currently_sched->priority));
    currently_sched->sched_state = rr_ChildState_Blocked;
    NO_ERR(seL4_TCB_UnbindVPMU(TCB(currently_sched->id)));
    currently_sched = NULL;
}

static inline microkit_channel rrer_ch_to_target_(microkit_channel ch) {
    if (ch % 2 == 0) return ch + 1;
    return ch - 1;
}

static inline void rrer_reschedule() {
    rrer_unschedule();

    // try to schedule another thread.
    rr_Child_t* next = rrer_get_next_scheduled();
    // if NULL then we are currently completely blocked.
    assert(next != NULL);

    rrer_schedule(next);
}

static inline void rrer_handle_ipc() {
    // methodology:
    // 1. Blocking send
    //    - Emulate blocking by only handling IPC when we realised that the current scheduled thread is blocked.
    //      And then setting it's priority to be it's original.
    //    - Emulate sending by having a dedicated sender? We need something that will block on the kernel.
    //      This ensures that it can be received by a NB recv.
    // 2. Blocking recv
    //    - Emulate blocking works by default. We notice that the PD is blocked, and set it as blocked and
    //      settign the priority to be it's original.
    //    - Emulating recv is fine, when we handle IPC we just have to reschedule the sender and the receiver.
    // 3. NB send
    //    - Emulate nonblocking. No comment.
    //    - Emulate send - We MUST receive the message so that we can check if it can be actually sent.
    //      Not sure how to do this. In order to actually receive the message, we must be determined
    //      as "blocked by recv" by the kernel. And then we just need to perform an NBSend.
    // 3. NB recv
    //    - Emulate nonblocking. No comment.
    //    - Emulate recv - We must be able to have a TCB that is determined as blocked by send, whenever
    //      a blocking send is.
    seL4_Word badge;
    for (seL4_MessageInfo_t result = seL4_NBRecv(INPUT_CAP, &badge, REPLY_CAP); badge != 0; result = seL4_NBRecv(INPUT_CAP, &badge, REPLY_CAP)) {
        LOG("Received from 0x%lx\n", badge);
        // the badge for microkit is the index of the 1 bit, which indicates the index.
        seL4_Word idx = 0;
        do {
            if (badge & 1)
            {
                // find target
                seL4_Word ch = rrer_ch_to_target_(idx);
                LOG("Forwarding to 0x%lx, %lu\n", ch, idx);
                seL4_NBSend(ch + BASE_OUTPUT_NOTIFICATION_CAP, result);

                // we must do a reschedule now.
                // and also unblock the target if it was blocked.
                if (children_arr[ch].sched_state == rr_ChildState_Blocked) {
                    children_arr[ch].sched_state = rr_ChildState_Schedulable;
                }
                rrer_reschedule();
            }
            idx++;
            badge >>= 1;
        } while (badge != 0);
    }
}

// main
static inline void rrer_main() {
    for (int i = 0; i < children_num; i++) {
        LOG(
            "id: %lu, priority: %lu, sched_state: %lu\n", 
            children_sched_queue[i]->id,
            children_sched_queue[i]->priority,
            (seL4_Word)children_sched_queue[i]->sched_state
        );
    }
    seL4_Word counter = 0;
    while (true) {
        // guaranteed to preempt ourself.
        if (scheduled_next) NO_ERR(seL4_TCB_Resume(TCB(currently_sched->id)));
        // or just yield a couple times
        else for (int i = 0; i < 100; i++) seL4_Yield();

        scheduled_next = false;
        // process and pass through notifications.
        rrer_handle_ipc();

        switch (sched_state) {
            case rr_SchedState_None: {
                rr_Child_t* next = rrer_get_next_scheduled();
                assert(next != NULL);
                rrer_schedule(next);
            } break;
            case rr_SchedState_Scheduled: {

                seL4_Word diff = rrer_cyclecount_diff_(counter);

                // check if the counter has increased since last.
                if (diff == 0) {
                    sched_state = rr_SchedState_Blocked;
                }
                counter += diff;
            } break;
            case rr_SchedState_Blocked: {
                // It is highly likely that that thread is blocked, so we'll set it to blocked,
                // and set it's priority back to normal.
                rrer_reschedule();
            } break;
        }
    }
}

