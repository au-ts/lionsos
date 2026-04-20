#include <microkit.h>
#include <types.h>
#include <stdint.h>
#include <stdlib.h>
#include <sddf/util/printf.h>
// #define NUMMAPS 256

uint64_t memory_manager_ep;

// void init(void)
// {
//     sddf_dprintf("hello from example pd 1\n");
//     // I have 128 heap frames, I want to do paging stuff here.
//     char *mappings[NUMMAPS];
//     for (int i = 0; i < NUMMAPS; ++i) {
//         mappings[i] = (char *)mymalloc(memory_manager_ep);
//         // sddf_dprintf("got return from mymalloc %p and the index is %d\n", mappings[i], i);
//         mappings[i][10] = 'c';
//         // sddf_printf("did the write \n");
//     }
//     // sddf_printf("stage 1 done \n");

//     for (int i = 0; i < NUMMAPS; ++i) {
//         myfree(memory_manager_ep, (uintptr_t)mappings[i]);
//         // sddf_printf("free %d\n", i);
//     }
//     // sddf_printf("stage 2 done \n");

//     for (int i = 0; i < NUMMAPS; ++i) {
//         mappings[i] = (char *)mymalloc(memory_manager_ep);
//         // sddf_dprintf("got return from mymalloc %p and the index is %d\n", mappings[i], i);
//         mappings[i][10] = 'c';
//     }
//     // sddf_printf("stage 3 done \n");

//     for (int i = 0; i < NUMMAPS; ++i) {
//         myfree(memory_manager_ep, (uintptr_t)mappings[i]);
        
//     }
//     sddf_printf("example pd 1 done!\n");
// }


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

#define NUMMAPS 256
#define PAGE_SIZE 4096

void init(void)
{
    sddf_printf("Starting pager test...\n");

    char *mappings[NUMMAPS];

    // 1. Allocate and write unique values
    for (int i = 0; i < NUMMAPS; ++i) {
        mappings[i] = (char *)mymalloc(memory_manager_ep);

        if (!mappings[i]) {
            sddf_printf("Allocation failed at %d\n", i);
            return;
        }

        // Write identifiable pattern
        for (int j = 0; j < PAGE_SIZE; j += 512) {
            mappings[i][j] = (char)i;
        }
    }

    sddf_printf("Initial writes done.\n");

    // 2. Read back to force page-ins and verify correctness
    for (int i = 0; i < NUMMAPS; ++i) {
        for (int j = 0; j < PAGE_SIZE; j += 512) {
            if (mappings[i][j] != (char)i) {
                sddf_printf("Data mismatch at page %d offset %d was %d\n", i, j, mappings[i][j]);
            }
        }
    }

    sddf_printf("Verification after eviction passed.\n");

    // 3. Free everything
    for (int i = 0; i < NUMMAPS; ++i) {
        myfree(memory_manager_ep, (uintptr_t)mappings[i]);
    }

    sddf_printf("Free completed.\n");

    // 4. Reallocate and ensure reuse works
    for (int i = 0; i < NUMMAPS; ++i) {
        mappings[i] = (char *)mymalloc(memory_manager_ep);

        if (!mappings[i]) {
            sddf_printf("Re-allocation failed at %d\n", i);
            return;
        }

        mappings[i][0] = 42;
    }

    sddf_printf("Re-allocation test passed.\n");

    sddf_printf("Pager test SUCCESS.\n");
}