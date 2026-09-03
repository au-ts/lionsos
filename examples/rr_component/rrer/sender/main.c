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

#define ENDPOINT(x) (BASE_ENDPOINT_CAP + x)
// communication flow:
// 1. The chosen thread

seL4_Word main_ch = 0;
uint8_t *per_thread_recv_queue_mem = NULL;
seL4_Word per_thread_recv_queue_size = 0;

queue_t *queues = NULL;
seL4_Word *rr_ipc_target_child_id = NULL;

void init()
{
    LOG("Starting sender!\n");
    assert(per_thread_recv_queue_mem != NULL);
    assert(per_thread_recv_queue_size != 0);
    rr_ipc_target_child_id = (seL4_Word *)per_thread_recv_queue_mem;
    queues = (queue_t *)(per_thread_recv_queue_mem + sizeof(seL4_Word));
    // if there's noone scheduled we ignore.
    LOG("target child: %lx\n", *rr_ipc_target_child_id);
    if (*rr_ipc_target_child_id == NO_THREAD_SCHEDULED) {
        LOG("No target, yielding\n");
        return;
    }
    queue_t* queue = queues + *rr_ipc_target_child_id;
    if (queue_len(queue) == 0) {
        LOG("No messages, yielding\n");
        return;
    }
    ipc_t ipc = queue_peek(queue);
    if (badge_is_ntfn(ipc.badge)) {
        LOG("Sending ntfn\n");
        seL4_Signal(BASE_OUTPUT_NOTIFICATION_CAP + ipc.channel);
    }
    else {
        LOG("Sending msg\n");
        seL4_MessageInfo_t msg = ipc_handler_read_msg(&queues->handler, ipc);
        seL4_Send(BASE_ENDPOINT_CAP + ipc.channel, msg);
    }
    queue_pop_ignore(queues);
    LOG("Message sent!\n");
}

void notified(microkit_channel ch)
{
}
