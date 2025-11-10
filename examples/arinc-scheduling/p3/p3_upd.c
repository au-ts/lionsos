#include <microkit.h>

#include <sddf/util/printf.h>

void timeTriggered(void)
{
    // Implement application logic here!
    
    sddf_dprintf("%s | timeTriggered\n", microkit_name);
}

void init(void)
{
    // User process setup here
}

void notified(microkit_channel ch)
{
    timeTriggered();

    // When we return from this notified function, we will go back to
    // being a passive thread.
}