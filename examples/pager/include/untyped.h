#ifndef _UNTYPED_H
#define _UNTYPED_H

#include <sel4/bootinfo_types.h>

typedef struct {
    // seL4_CNode untyped_cnode_cptr;
    seL4_SlotRegion untypeds;
    seL4_UntypedDesc untypedList[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
} capDLBootInfo_t;


// typedef struct seL4_SlotRegion {
//     seL4_SlotPos start; /* first CNode slot position OF region */
//     seL4_SlotPos end;   /* first CNode slot position AFTER region */
// } seL4_SlotRegion;

// typedef struct seL4_UntypedDesc {
//     seL4_Word  paddr;   /* physical address of untyped cap  */
//     seL4_Uint8 sizeBits;/* size (2^n) bytes of each untyped */
//     seL4_Uint8 isDevice;/* whether the untyped is a device  */
//     seL4_Uint8 padding[sizeof(seL4_Word) - 2 * sizeof(seL4_Uint8)];
// } seL4_UntypedDesc;

#endif