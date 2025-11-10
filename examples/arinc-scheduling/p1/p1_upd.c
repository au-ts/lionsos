#include <microkit.h>

#include <sddf/util/printf.h>
#include <p1/p1.h>

#define PORT_TO_INIT_PD 0

volatile sb_queue_int32_t_1_t *write_port_queue_1 = (volatile sb_queue_int32_t_1_t *) 0x10000000;

bool put_write_port(const int32_t *data) {
  sb_queue_int32_t_1_enqueue((sb_queue_int32_t_1_t *) write_port_queue_1, (int32_t *) data);

  return true;
}

int value = 0;

void initialise(void)
{
    sddf_dprintf("%s: Init\n", microkit_name);

    put_write_port(&value);
}

void timeTriggered(void)
{
    // Implement application logic here!
    value = value + 1;

    put_write_port(&value);

    sddf_dprintf("%s: Sent %d\n", microkit_name, value);
}

void init(void)
{
    // User process setup here
    sb_queue_int32_t_1_init((sb_queue_int32_t_1_t *) write_port_queue_1);

    initialise();

    microkit_notify(PORT_TO_INIT_PD);
}

void notified(microkit_channel ch)
{
    timeTriggered();

    // When we return from this notified function, we will go back to
    // being a passive thread.
}