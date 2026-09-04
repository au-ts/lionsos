#ifndef _UNTYPED_H
#define _UNTYPED_H

#include <sel4/bootinfo_types.h>

typedef struct {
    // seL4_CNode untyped_cnode_cptr;
    seL4_SlotRegion untypeds;
    seL4_UntypedDesc untypedList[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
} capDLBootInfo_t;

#endif