#include <microkit.h>
#include <string.h>
#include <assert.h>
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "types.h"
#include <sddf/util/printf.h>
#define LOG(...) do {sddf_printf("SENDER [%s]| ", __func__); sddf_printf(__VA_ARGS__);} while (0)

#define ENDPOINT(x) (BASE_ENDPOINT_CAP + x)
// communication flow:
// 1. The chosen thread

seL4_Word main_ch = 0;
unsigned char *per_thread_recv_queue_mem = NULL;
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
    if (queue_len(queues) == 0) {
        LOG("No messages, yielding\n");
        return;
    }
    seL4_MessageInfo_t msg = queue_peek(queues);
    LOG("Sending message\n");
    seL4_Send(ENDPOINT(rr_ipc_target_child_id[0]), msg);
    queue_pop_ignore(queues);
    LOG("Message sent!\n");
}

void notified(microkit_channel ch)
{
}
