#include <microkit.h>
#include <string.h>
#include <assert.h>
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "types.h"

#define ENDPOINT(x) (BASE_ENDPOINT_CAP + x)

seL4_Word main_ch = 0;
unsigned char *per_thread_recv_queue_mem = NULL;
seL4_Word per_thread_recv_queue_size = 0;

queue_t* queues = NULL;
seL4_Word* curr_sched_child = NULL;
void init()
{
    assert(per_thread_recv_queue_mem != NULL);
    assert(per_thread_recv_queue_size != 0);
    curr_sched_child = (seL4_Word*)per_thread_recv_queue_mem;
    queues = (queue_t*)(per_thread_recv_queue_mem + sizeof(seL4_Word));
    // if there's noone scheduled we ignore.
    if (*curr_sched_child == (seL4_Word)-1) return;
    while (queue_len(queues) > 0) {
        seL4_MessageInfo_t msg = queue_peek(queues);
        seL4_Send(ENDPOINT(curr_sched_child[0]), msg);
        queue_pop_ignore(queues);
    }
}

void notified(microkit_channel ch)
{
}
