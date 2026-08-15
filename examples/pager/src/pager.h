#ifndef _PAGER_H
#define _PAGER_H

#include <stdint.h>
#include <microkit.h>
#include <sel4/bootinfo_types.h>

#include <untyped.h>
#include <frame_table.h>
#include <page_table.h>
#include <untyped.h>
#include <stdlib.h>
#include <mapping.h>

#include "cspace.h"
/**
 * TODO: do the actual implementation
 */
seL4_CapRights_t create_cap_rights(uint64_t fsr);

static seL4_Error map_frame(uint64_t frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                                 seL4_CapRights_t rights, seL4_ARM_VMAttributes attr);


/**
 * Refills unused_frame_list using untyped memory
 * 10 new frames.
 * TODO:
 * - page reclaim if necessary.
 * - could move to frame_table.c
 */
void refill_unused();
/**
 * Moves unused frame to used frame list 
 */
frame_t *get_unused_frame();
seL4_Error retype_map_pt(seL4_CPtr vspace, seL4_Word vaddr);
seL4_Error my_untyped_retype(
                          seL4_Word object_type,
                          seL4_Word size_bits,
                          uint32_t *retyped_cap_idx);

uintptr_t allocate_pager_memory(uint64_t size);

frame_t *get_frame_from_idx(uint32_t frame_idx);
#endif