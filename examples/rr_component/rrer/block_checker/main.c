#include <microkit.h>
#include <stdbool.h>
#include "types.h"
#include <sddf/util/printf.h>
#define LOG(...) do {sddf_printf("BLOCKER [%s]| ", __func__); sddf_printf(__VA_ARGS__);} while (0)

seL4_Word main_ch = UNSET_VALUE;

void init()
{
    assert(main_ch != UNSET_VALUE);
    // hmmm. how do I ensure that we do not accidentally starve the child...
    // if the block checker gets scheduled first it will always cause a notify before
    // the child could run, then triggering the target.
    // What we can do is always yield first, which ensures that the target
    // will always run for at least one slice, and at most two slices.
    LOG("First yield!\n");
    seL4_Yield();
    LOG("Notifying main!\n");
    microkit_notify(main_ch);
}

void notified(microkit_channel ch)
{
}
