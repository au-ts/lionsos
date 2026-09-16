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

/**
 * Description of the untypeds that survived system initialisation. The Microkit
 * tool writes one of these into the memory region declared with
 * prefill_bootinfo="post_capdl_untypeds", which the pager reads through
 * remaining_untypeds_vaddr.
 */
typedef struct {
    // seL4_CNode untyped_cnode_cptr;
    seL4_SlotRegion untypeds;
    seL4_UntypedDesc untypedList[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
} capDLBootInfo_t;

/**
 * Takes ownership of the pager's untyped memory. bootinfo_vaddr is where the
 * capDLBootInfo_t was written, untyped_cnode_cptr addresses the CNode holding
 * the untyped caps it describes.
 */
void untyped_init(uintptr_t bootinfo_vaddr, seL4_CPtr untyped_cnode_cptr);

/**
 * Retypes one object out of the pager's untyped memory into slot cap_idx of
 * destination_cnode, moving on to the next untyped once one is exhausted.
 */
seL4_Error untyped_alloc(seL4_Word object_type, seL4_Word size_bits,
                         uint32_t cap_idx, seL4_CPtr destination_cnode);

#endif
