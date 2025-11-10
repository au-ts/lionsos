#include <microkit.h>

#include <sddf/util/printf.h>
#include <p3/p3.h>

#define PORT_TO_INIT_PD 0

volatile sb_queue_int32_t_1_t *read_port_queue_1 = (volatile sb_queue_int32_t_1_t *) 0x10000000;
sb_queue_int32_t_1_Recv_t read_port_recv_queue;

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

void initialise()
{
    sddf_dprintf("%s: Init\n", microkit_name);
}

void timeTriggered(void)
{
    // Implement application logic here!
    int32_t value;
    get_read_port(&value);

    sddf_dprintf("%s: Received: %d\n", microkit_name, value);
}

void init(void)
{
    // User process setup here
    sb_queue_int32_t_1_Recv_init(&read_port_recv_queue, (sb_queue_int32_t_1_t *) read_port_queue_1);

    initialise();

    microkit_notify(PORT_TO_INIT_PD);
}

void notified(microkit_channel ch)
{
    timeTriggered();

    // When we return from this notified function, we will go back to
    // being a passive thread.
}