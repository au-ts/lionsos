/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _UNTYPED_H
#define _UNTYPED_H

#include <stdint.h>

#include "cspace.h"

#include <sel4/bootinfo_types.h>


typedef struct {
    // seL4_CNode untyped_cnode_cptr;
    seL4_SlotRegion untypeds;
    seL4_UntypedDesc untypedList[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
} capDLBootInfo_t;

// capDLBootInfo_t is bootinfo_vaddr, cnode holding untyped caps
void untyped_init(uintptr_t bootinfo_vaddr, seL4_CPtr untyped_cnode_cptr)
{
    capDLBootInfo_t *capDLBootInfo = (capDLBootInfo_t *) bootinfo_vaddr;

    untyped_cnode.cptr = untyped_cnode_cptr;
    untyped_cnode.start = capDLBootInfo->untypeds.start;
    // // TODO: is end empty?
    for (uint64_t i = capDLBootInfo->untypeds.start; i < capDLBootInfo->untypeds.end; i++) {
        untyped_cnode.caps[i].base_addr = capDLBootInfo->untypedList[i].paddr;
        untyped_cnode.caps[i].end_addr = untyped_cnode.caps[i].base_addr + (1ULL << capDLBootInfo->untypedList[i].sizeBits);
        untyped_cnode.caps[i].is_device = capDLBootInfo->untypedList[i].isDevice;
        untyped_cnode.caps[i].object_type = seL4_UntypedObject;
        untyped_cnode.end = i + 1;
        sddf_dprintf("i: %lu, 0x%lx-0x%lx: device? %d\n", i, untyped_cnode.caps[i].base_addr, untyped_cnode.caps[i].end_addr, untyped_cnode.caps[i].is_device);
    }
    update_active_ut_idx(&untyped_cnode);
    sddf_dprintf("cnode start: %d\n", untyped_cnode.start);
}

seL4_Error untyped_alloc(seL4_Word object_type, seL4_Word size_bits,
                         uint32_t cap_idx, seL4_CPtr destination_cnode,
                         uint32_t num)
{
    return do_untyped_retype(&untyped_cnode, object_type, size_bits, cap_idx,
                             destination_cnode, num);
}


#endif
