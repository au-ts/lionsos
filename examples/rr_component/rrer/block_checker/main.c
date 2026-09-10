#include <microkit.h>
#include <stdbool.h>
#include "types.h"
#include <sddf/util/printf.h>

seL4_Word main_ch = UNSET_VALUE;

void init()
{
}

void notified(microkit_channel ch)
{
    assert(main_ch != UNSET_VALUE);
    // We can almost certainly expect the block_checker to always run first because the
    // block_checker is always schedulable whereas the other child PDs will be scheduled
    // later.
    LOG("First yield!\n");
    seL4_Yield();
    LOG("Notifying main!\n");
    microkit_notify(main_ch);
}
