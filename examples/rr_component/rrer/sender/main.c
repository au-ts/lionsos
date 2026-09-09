#include <microkit.h>
#include <string.h>
#include <assert.h>
#include "interfaces/sel4_client.h"
#include "sel4/sel4_arch/constants.h"
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "sel4/syscalls_mcs.h"
#include "types.h"
#include <sddf/util/printf.h>

#define LOG(...) do {sddf_printf("SENDER [%s]| ", __func__); sddf_printf(__VA_ARGS__);} while (0)
// #define LOG(...)

#define ENDPOINT(x) (BASE_ENDPOINT_CAP + x)

seL4_Word main_ch = 0;
uint8_t *per_thread_recv_queue_mem = NULL;
seL4_Word per_thread_recv_queue_size = 0;

rrer_queue_t *queues = NULL;
seL4_Word *rr_ipc_target_child_id = NULL;

void init()
{
    LOG("Starting sender!\n");
    assert(per_thread_recv_queue_mem != NULL);
    assert(per_thread_recv_queue_size != 0);
    rr_ipc_target_child_id = (seL4_Word *)per_thread_recv_queue_mem;
    queues = (rrer_queue_t *)(per_thread_recv_queue_mem + sizeof(seL4_Word));
    // if there's noone scheduled we ignore.
    LOG("target child: %lx\n", *rr_ipc_target_child_id);
    if (*rr_ipc_target_child_id == NO_THREAD_SCHEDULED) {
        LOG("No target, yielding\n");
        return;
    }
    rrer_queue_t* queue = queues + *rr_ipc_target_child_id;
    if (rrer_queue_len(queue) == 0) {
        LOG("No messages, yielding\n");
        return;
    }
    rrer_ipc_t ipc = rrer_queue_peek(queue);
    if (rrer_badge_is_ntfn(ipc.badge)) {
        LOG("Sending ntfn ch %lu\n", rrer_source_ch_to_target_ch(ipc.channel));
        seL4_Signal(BASE_OUTPUT_NOTIFICATION_CAP + rrer_source_ch_to_target_ch(ipc.channel));
        rrer_queue_pop_ignore(queue);
        LOG("Ntfn sent!\n");
    }
    else {
        LOG("Sending msg ch %lu\n", rrer_source_ch_to_target_ch(ipc.channel));
        seL4_MessageInfo_t to_send = rrer_ipc_handler_read_msg(&queues->handler, ipc);
        // If this get's preempted, what happens?
        // The reply cap will get invalidated (IPC gets cancelled),
        // so there are two options (so far that I can think of) here:
        // 1. per pd sender pds
        // 2. leave it as unsupported (which is what i'll do for now).
        seL4_MessageInfo_t replied = seL4_Call(BASE_ENDPOINT_CAP + rrer_source_ch_to_target_ch(ipc.channel), to_send);
        rrer_queue_pop_ignore(queue);
        LOG("Call finished!\n");
        // Send the reply, just through using the current ipc buffer.
        seL4_Send(BASE_ENDPOINT_CAP + main_ch, replied);
    }
}

void notified(microkit_channel ch)
{
    LOG("Notified!\n");
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) 
{
    LOG("Protected!\n");
    return microkit_msginfo_new(0, 0);
}
