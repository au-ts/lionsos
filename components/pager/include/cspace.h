/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _CSPACE_H
#define _CSPACE_H


#include <stdint.h>
#include <stdbool.h>
#include <sel4/sel4.h>

#define MAX_NUM_CAP_SLOTS 512
#define GET_OBJECT_SIZE(object_type, size_bits) (1ULL << get_object_size_bits(object_type, size_bits))

typedef struct {
    uintptr_t base_addr;
    uintptr_t end_addr;
    uint8_t is_device;
    uint8_t object_type;
    uint32_t parent;
    uint32_t child;
    uint32_t next;
} cap_desc_t;

typedef struct {
    cap_desc_t caps[MAX_NUM_CAP_SLOTS];
    uint32_t start;         // start index
    uint32_t end;           // end index
    uint32_t active_ut_idx; // Index of untyped to be allocated for kernel objects
    seL4_CPtr cptr;         // CNode cap addr
} cnode_specs_t;

#define PAGER_CNODE_DEPTH 58

/*
 * Retype num objects into consecutive slots from retyped_cap_idx. The window is
 * halved rather than abandoned when the active untyped cannot fit it, so we do
 * not throw away the tail of every untyped we walk past.
 */
seL4_Error do_untyped_retype(cnode_specs_t *cnode_specs, seL4_Word object_type,
    seL4_Word size_bits, uint32_t retyped_cap_idx, seL4_CPtr destination_cnode,
    uint32_t num) {

    uint32_t done = 0;
    while (done < num) {
        uint32_t want = num - done;
        seL4_Error error = seL4_Untyped_Retype(cnode_specs->cptr + cnode_specs->active_ut_idx,
                                    object_type,
                                    size_bits,
                                    destination_cnode, 0, 0,
                                    retyped_cap_idx + done, want);

        while (error == seL4_NotEnoughMemory && cnode_specs->active_ut_idx < cnode_specs->end) {
            if (want > 1) {
                want >>= 1;
            } else {
                ++cnode_specs->active_ut_idx;
            }
            error = seL4_Untyped_Retype(cnode_specs->cptr + cnode_specs->active_ut_idx,
                                    object_type,
                                    size_bits,
                                    destination_cnode, 0, 0,
                                    retyped_cap_idx + done, want);
        }
        if (error != seL4_NoError) {
            sddf_dprintf("Error: failed to retype %u objects of type %lu, cptr: 0x%lx, size_bits: %lu - error: %d - ut idx = %d\n", want, object_type, cnode_specs->cptr + cnode_specs->active_ut_idx, size_bits, error, cnode_specs->active_ut_idx);
            return error;
        }
        done += want;
    }
    return seL4_NoError;
}

uint8_t get_object_size_bits(seL4_Word object_type, seL4_Word size_bits)
{
    switch (object_type) {
    /* Generic objects. */
    case seL4_UntypedObject:
        return size_bits;
    case seL4_TCBObject:
        return seL4_TCBBits;
    case seL4_EndpointObject:
        return seL4_EndpointBits;
    case seL4_NotificationObject:
        return seL4_NotificationBits;
    case seL4_CapTableObject:
        return (seL4_SlotBits + size_bits);
    default:
        // TODO: double-check this
        return size_bits;
    }
}

seL4_Word max_size_bits(seL4_Word size)
{
    seL4_Word i = 63;
    while (((1ULL << i) & size) == 0) {
        i--;
    }
    return i;
}

void clear_cnode_specs_entry(cnode_specs_t *cnode_specs, uint32_t ut_idx) {
    cnode_specs->caps[ut_idx].base_addr = 0;
    cnode_specs->caps[ut_idx].end_addr = 0;
    cnode_specs->caps[ut_idx].is_device = 0;
    cnode_specs->caps[ut_idx].object_type = 0;
    cnode_specs->caps[ut_idx].parent = 0;
    cnode_specs->caps[ut_idx].child = 0;
}

void update_active_ut_idx(cnode_specs_t *cnode_specs) {
    // TODO: find a proper untyped for PT objects, not the first one is used by capDL initialiser
    // uint32_t non_dev_mem_id = 0;
    uint32_t i;
    for (i = cnode_specs->start; i < cnode_specs->end; i++) {
        if (cnode_specs->caps[i].is_device == false && cnode_specs->caps[i].object_type == seL4_UntypedObject) {
            // if (non_dev_mem_id == 5) {
                cnode_specs->active_ut_idx = i;
                break;
            // }
            // non_dev_mem_id++;
        }
    }
    if (i < cnode_specs->end) {
        sddf_dprintf("Found an untyped for kernel objects: ut idx: 0x%x, paddr: 0x%lx\n", cnode_specs->active_ut_idx, cnode_specs->caps[i].base_addr);
    } else {
        sddf_dprintf("[Error] failed to find an available untyped for kernel objects allocation\n");
    }
}

/* Grant and grant-reply mean nothing to a frame cap. */
static inline seL4_CapRights_t create_cap_rights(bool is_write) {
    return seL4_CapRights_new(0, 0, 1, is_write);
}

static inline seL4_CapRights_t create_ep_cap_rights(void) {
    return seL4_CapRights_new(0, 1, 1, 1);
}

#endif
