#include "microkit.h"
#include <sddf/util/printf.h>

void recurse3()
{
    sddf_dprintf("rec3\n");
    microkit_internal_crash(1);
}

void recurse2()
{
    sddf_dprintf("rec2\n");
    // Force a large stack frame each call
    volatile char big[4096];

    // touch memory so compiler doesn't optimise it away
    big[0] = 1;
    big[4095] = (char)(1);

    recurse3();
}

void recurse1()
{
    sddf_dprintf("rec1\n");
    volatile char big[4096];

    // touch memory so compiler doesn't optimise it away
    big[0] = 1;
    big[4095] = (char)(1);

    recurse2();
}


void init(void)
{
    recurse1();
}

void notified(microkit_channel ch) {
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    microkit_msginfo msg;
    return msg;
}