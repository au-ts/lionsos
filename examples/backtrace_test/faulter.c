#include <microkit.h>
#include <sddf/util/printf.h>
#include <stddef.h>
#include <stdint.h>
#define LOG(...) sddf_printf("FAULTER | " __VA_ARGS__)

const char *timestamp = __TIMESTAMP__;
// Get a random-ish pointer to low-ish memory
uintptr_t happy = 0;

void recurseFault(int depth)
{
    if (depth == 0) {
        *(volatile int *)happy;
        return;
    }
    recurseFault(--depth);
}

void init()
{
    LOG("Faulter initialised!\n");
    recurseFault(4);
    LOG("After dereference\n");
}
void notified(microkit_channel ch)
{
    LOG("Notified!\n");
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
}
