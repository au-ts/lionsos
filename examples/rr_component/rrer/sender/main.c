#include <microkit.h>
#include <string.h>

seL4_Word main_ch = 0;
unsigned char *per_thread_recv_queue_mem = NULL;
seL4_Word per_thread_recv_queue_size = 0;

void init()
{
}

void notified(microkit_channel ch)
{
}
