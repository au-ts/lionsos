#include <microkit.h>

#include <sddf/util/printf.h>
#include <p2/p2.h>

#define PORT_TO_INIT_PD 0

volatile sb_queue_int32_t_1_t *read_port_queue_1 = (volatile sb_queue_int32_t_1_t *) 0x10000000;
sb_queue_int32_t_1_Recv_t read_port_recv_queue;
volatile sb_queue_int32_t_1_t *write_port_queue_1 = (volatile sb_queue_int32_t_1_t *) 0x10001000;

int32_t last_read_port_payload;

bool get_read_port(int32_t *data) {
    sb_event_counter_t numDropped;
    int32_t fresh_data;
    bool isFresh = sb_queue_int32_t_1_dequeue((sb_queue_int32_t_1_Recv_t *) &read_port_recv_queue, &numDropped, &fresh_data);
    if (isFresh) {
    last_read_port_payload = fresh_data;
    }
    *data = last_read_port_payload;
    return isFresh;
}

bool put_write_port(const int32_t *data) {
    sb_queue_int32_t_1_enqueue((sb_queue_int32_t_1_t *) write_port_queue_1, (int32_t *) data);

    return true;
}

int value = 0;

void initialise(void)
{
    // add initialization code here
    sddf_dprintf("%s: Init\n", microkit_name);

    put_write_port(&value); 
}

void timeTriggered(void)
{
  value = value + 1;

  put_write_port(&value);

  sddf_dprintf("%s: Sent %d\n", microkit_name, value);
}

void init(void)
{
    // User process setup here
    sb_queue_int32_t_1_Recv_init(&read_port_recv_queue, (sb_queue_int32_t_1_t *) read_port_queue_1);

    sb_queue_int32_t_1_init((sb_queue_int32_t_1_t *) write_port_queue_1);
 
    initialise();

    microkit_notify(PORT_TO_INIT_PD);
}

void notified(microkit_channel ch)
{
    initialise();
    timeTriggered();

    // When we return from this notified function, we will go back to
    // being a passive thread.
}