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

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    microkit_dbg_puts("what the hell???\n");
    microkit_dbg_puts("Channel: ");
    microkit_dbg_put32(ch);
    microkit_dbg_puts(" Msginfo: ");
    microkit_dbg_put32(msginfo.words[0]);
    microkit_dbg_puts("\n");
    return microkit_msginfo_new(0, 0);
}
