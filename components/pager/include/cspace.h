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

seL4_Error do_untyped_retype(cnode_specs_t *cnode_specs, seL4_Word object_type,
    seL4_Word size_bits, uint32_t retyped_cap_idx, seL4_CPtr destination_cnode,
    uint32_t num);

uint8_t get_object_size_bits(seL4_Word object_type, seL4_Word size_bits);

seL4_Word max_size_bits(seL4_Word size);

seL4_Error get_untyped_at_paddr(cnode_specs_t *cnode_specs,
                                seL4_Word target_paddr,
                                uint32_t *target_ut_idx);

void clear_cnode_specs_entry(cnode_specs_t *cnode_specs, uint32_t ut_idx);

void update_active_ut_idx(cnode_specs_t *cnode_specs);

/* Grant and grant-reply mean nothing to a frame cap. */
static inline seL4_CapRights_t create_cap_rights(bool is_write) {
    return seL4_CapRights_new(0, 0, 1, is_write);
}

#endif
