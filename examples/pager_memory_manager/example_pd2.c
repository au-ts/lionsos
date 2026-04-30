#include <microkit.h>
#include <types.h>
#include <stdint.h>
#include <stdlib.h>
#include <sddf/util/printf.h>
#define NUMMAPS 256

uint64_t memory_manager_ep;

void init(void)
{
    sddf_dprintf("hello from example pd 2\n");
    // I have 128 heap frames, I want to do paging stuff here.
    char *mappings[NUMMAPS];
    for (int i = 0; i < NUMMAPS; ++i) {
        mappings[i] = (char *)mymalloc(memory_manager_ep);
        // sddf_dprintf("got return from mymalloc %p and the index is %d\n", mappings[i], i);
        mappings[i][10] = 'c';
    }

    for (int i = 0; i < NUMMAPS; ++i) {
        myfree(memory_manager_ep, (uintptr_t)mappings[i]);
    }

    for (int i = 0; i < NUMMAPS; ++i) {
        mappings[i] = (char *)mymalloc(memory_manager_ep);
        // sddf_dprintf("got return from mymalloc %p and the index is %d\n", mappings[i], i);
        mappings[i][10] = 'c';
    }

    for (int i = 0; i < NUMMAPS; ++i) {
        myfree(memory_manager_ep, (uintptr_t)mappings[i]);
    }
    sddf_printf("example pd 2 done!\n");
    // for (int i = 0; i < 400; ++i) {
    //     sddf_printf("example pd 2 done!\n");
    // }
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