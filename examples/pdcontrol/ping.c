#include <microkit.h>

seL4_Word pongch;

void init()
{
    microkit_dbg_puts("ping init\n");
    microkit_notify(pongch);
}
void notified(microkit_channel ch)
{
    microkit_dbg_puts("ping notified!\n");
    microkit_notify(pongch);
}
