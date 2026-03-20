#include <microkit.h>
#include <types.h>
#include <stdint.h>
void init(void)
{
    // I have 128 heap frames, I want to do paging stuff here.
    char *mappings[256];
    for (int i = 0; i < 256; ++i) {
        mappings[i] = (char *)malloc();
        mappings[i][10] = 'c';
    }

    for (int i = 0; i < 256; ++i) {
        free((uintptr_t)mappings[i]);
    }

    for (int i = 0; i < 256; ++i) {
        mappings[i] = (char *)malloc();
        mappings[i][10] = 'c';
    }

    for (int i = 0; i < 256; ++i) {
        free((uintptr_t)mappings[i]);
    }
}


// NOT USED BELOW:
seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    // this is not used
    seL4_MessageInfo_t ret;
    return ret;
}



void notified(microkit_channel ch)
{
    // this may not be required
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // not required.
    return seL4_False;
}