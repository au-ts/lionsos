#include <microkit.h>
#include <sddf/util/printf.h>

// @kwinter: For now this will always be 0, but we will want to patch
// this information in in the future.
#define SCHEDULER_CH 0
#define USER_PD 1

void partition_init()
{
    // Place all initialisation code here. Such as port creation etc.
}

void partition_startup()
{
    // This function is called at the start of every partition timeslice.
    // For now this can handle any port management needed
}

void notified(microkit_channel ch)
{
    if (ch == SCHEDULER_CH) {
        partition_startup();
        microkit_notify(USER_PD);
    } else if (USER_PD) {
        // THIS SHOULD ONLY BE FOR THE USER PD TO SIGNAL ITS FINISHED INIT
        microkit_notify(SCHEDULER_CH);
    }
}

void init(void)
{
    sddf_dprintf("%s | INIT!\n", microkit_name);
    partition_init();

    // Notify the scheduler to mark the init procedure as finished
    microkit_notify(SCHEDULER_CH);
}