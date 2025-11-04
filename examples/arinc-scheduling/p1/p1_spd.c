#include <microkit.h>
#include <sddf/util/printf.h>

// @kwinter: For now this will always be 0, but we will want to patch
// this information in in the future.
#define SCHEDULER_CH 0

void partition_init()
{
    // Place all initialisation code here. Such as port creation etc.
}

void partition_startup()
{
    // This function is called at the start of every partition timeslice.
    // For now this can handle any port management needed
}

void periodic_start()
{
    // For now this can be the periodic function we run every timeslice.
    // Eventually, this will call into a seperate user process
}

void notified(microkit_channel ch)
{
    sddf_dprintf("%s Notified!\n", microkit_name);

    partition_startup();
    periodic_start();
}

void init(void)
{
    sddf_dprintf("%s | INIT!\n", microkit_name);
    partition_init();

    // Notify the scheduler to mark the init procedure as finished
    microkit_notify(SCHEDULER_CH);
}