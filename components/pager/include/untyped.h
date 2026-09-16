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
void untyped_init(uintptr_t bootinfo_vaddr, seL4_CPtr untyped_cnode_cptr);

seL4_Error untyped_alloc(seL4_Word object_type, seL4_Word size_bits,
                         uint32_t cap_idx, seL4_CPtr destination_cnode,
                         uint32_t num);

#endif
