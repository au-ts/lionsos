/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "untyped.h"

#include <sddf/util/printf.h>

/* All untyped memory left over after system initialisation. */
static cnode_specs_t post_boot_cnode;

void untyped_init(uintptr_t bootinfo_vaddr, seL4_CPtr cnode_cptr)
{
    capDLBootInfo_t *capDLBootInfo = (capDLBootInfo_t *) bootinfo_vaddr;

    post_boot_cnode.cptr = cnode_cptr;
    post_boot_cnode.start = capDLBootInfo->untypeds.start;
    // // TODO: is end empty?
    for (uint64_t i = capDLBootInfo->untypeds.start; i < capDLBootInfo->untypeds.end; i++) {
        post_boot_cnode.caps[i].base_addr = capDLBootInfo->untypedList[i].paddr;
        post_boot_cnode.caps[i].end_addr = post_boot_cnode.caps[i].base_addr + (1ULL << capDLBootInfo->untypedList[i].sizeBits);
        post_boot_cnode.caps[i].is_device = capDLBootInfo->untypedList[i].isDevice;
        post_boot_cnode.caps[i].object_type = seL4_UntypedObject;
        post_boot_cnode.end = i + 1;
        sddf_dprintf("i: %lu, 0x%lx-0x%lx: device? %d\n", i, post_boot_cnode.caps[i].base_addr, post_boot_cnode.caps[i].end_addr, post_boot_cnode.caps[i].is_device);
    }
    update_active_ut_idx(&post_boot_cnode);
    sddf_dprintf("cnode start: %d\n", post_boot_cnode.start);
}

seL4_Error untyped_alloc(seL4_Word object_type, seL4_Word size_bits,
                         uint32_t cap_idx, seL4_CPtr destination_cnode)
{
    return do_untyped_retype(&post_boot_cnode, object_type, size_bits, cap_idx,
                             destination_cnode);
}
