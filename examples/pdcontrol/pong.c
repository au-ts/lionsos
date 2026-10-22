#include <microkit.h>

seL4_Word pingch;

void init()
{
    microkit_dbg_puts("pong init\n");
}

seL4_Word curr = 0;
void notified(microkit_channel ch)
{
    microkit_dbg_puts("pong notified!\n");
    if (curr++ > 5)
        return;
    microkit_notify(pingch);
}
