#include <microkit.h>

#include <sddf/util/printf.h>

#define PORT_TO_INIT_PD 0


void initialise()
{
    sddf_dprintf("%s: Init\n", microkit_name);
}

void timeTriggered(void)
{
    sddf_dprintf("%s: timeTriggered\n", microkit_name);
}

void init(void)
{
    initialise();

    microkit_notify(PORT_TO_INIT_PD);
}

void notified(microkit_channel ch)
{
    timeTriggered();
}