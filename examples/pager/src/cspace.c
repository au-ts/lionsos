
#pragma once
#include "cspace.h"
#include <sddf/util/printf.h>

seL4_Error do_untyped_retype(cnode_specs_t *cnode_specs, seL4_Word object_type,
    seL4_Word size_bits, uint32_t *retyped_cap_idx, seL4_CPtr destination_cnode) {


    // seL4_Error error = my_untyped_retype3(cnode_specs, ut_idx, object_type, size_bits, retyped_cap_idx);
    seL4_Error error = seL4_Untyped_Retype(cnode_specs->cptr + cnode_specs->active_ut_idx,
                                object_type,
                                size_bits,
                                destination_cnode, 0, 0,
                                *retyped_cap_idx, 1);

    while (error == seL4_NotEnoughMemory) {
        ++cnode_specs->active_ut_idx;
        error = seL4_Untyped_Retype(cnode_specs->cptr + cnode_specs->active_ut_idx,
                                object_type,
                                size_bits,
                                destination_cnode, 0, 0,
                                *retyped_cap_idx, 1);
    }
    if (error != seL4_NoError) {
        sddf_dprintf("Error: failed to retype an object type %lu, cptr: 0x%lx, size_bits: %lu - error: %d - ut idx = %d\n", object_type, cnode_specs->cptr + cnode_specs->active_ut_idx, size_bits, error, cnode_specs->active_ut_idx);
        return error;
    }
    ++(*retyped_cap_idx);
    return error;
}

seL4_Word max_size_bits(seL4_Word size)
{
    seL4_Word i = 63;
    while (((1ULL << i) & size) == 0) {
        i--;
    }
    return i;
}

// TODO: check if this makes sense to go to libsel4
// https://github.com/seL4/seL4_libs/blob/master/libsel4vka/arch_include/x86/vka/arch/object.h#L62
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

seL4_Error get_untyped_at_paddr(cnode_specs_t *cnode_specs,
                                seL4_Word target_paddr,
                                uint32_t *target_ut_idx)
{
    uint32_t ut_idx = cnode_specs->end;
    for (uint32_t i = cnode_specs->start; i < cnode_specs->end; i++) {
        if (cnode_specs->caps[i].base_addr <= target_paddr &&
            target_paddr < cnode_specs->caps[i].end_addr &&
            cnode_specs->caps[i].object_type == seL4_UntypedObject) {
            ut_idx = i;
            break;
        }
    }
    if (ut_idx == cnode_specs->end) {
        sddf_dprintf("Error: Untyped containing physical address 0x%lx is not found\n", target_paddr);
        return seL4_InvalidArgument;
    }
    /* sddf_dprintf("Found the untyped containing physical address: 0x%lx\n", target_paddr); */
    /* sddf_dprintf("ut idx: %u, base_addr: 0x%lx, end_addr: 0x%lx\n", ut_idx, cnode_specs->caps[ut_idx].base_addr, cnode_specs->caps[ut_idx].end_addr); */

    seL4_Error error;

    // Divide untyped to smaller ones
    // TODO: figure out what's the maxinum and minimum bits here
    for (int bits = 63; bits >= 12; bits--) {
        while (target_paddr - cnode_specs->caps[ut_idx].base_addr >= (1ULL << bits)) {
            error = untyped_retype(cnode_specs, ut_idx, seL4_UntypedObject, bits, NULL);
            if (error != seL4_NoError){
                sddf_dprintf("Error: failed to divide an untyped(%d)[0x%lx-0x%lx] to a smaller untyped with size_bits=%d\n",
                             ut_idx,
                             cnode_specs->caps[ut_idx].base_addr,
                             cnode_specs->caps[ut_idx].end_addr,
                             bits);
                return error;
            }
        }
    }

    *target_ut_idx = ut_idx;
    return seL4_NoError;
}

void clear_cnode_specs_entry(cnode_specs_t *cnode_specs, uint32_t ut_idx)
{
    cnode_specs->caps[ut_idx].base_addr = 0;
    cnode_specs->caps[ut_idx].end_addr = 0;
    cnode_specs->caps[ut_idx].is_device = 0;
    cnode_specs->caps[ut_idx].object_type = 0;
    cnode_specs->caps[ut_idx].parent = 0;
    cnode_specs->caps[ut_idx].child = 0;
}

void update_active_ut_idx(cnode_specs_t *cnode_specs)
{
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

// deprecated ***

seL4_Error untyped_retype(cnode_specs_t *cnode_specs,
                          uint32_t ut_idx,
                          seL4_Word object_type,
                          seL4_Word size_bits,
                          uint32_t *retyped_cap_idx)
{
    // @terryb: need to update this if we remove self-ref cap at slot 0
    seL4_Error error = seL4_Untyped_Retype(cnode_specs->cptr + ut_idx,
                                object_type,
                                size_bits,
                                cnode_specs->cptr, 0, 0,
                                cnode_specs->end, 1);
    if (error != seL4_NoError) {
        sddf_dprintf("Error: failed to retype an object type %lu, cptr: 0x%lx, size_bits: %lu - error: %d\n", object_type, cnode_specs->cptr + ut_idx, size_bits, error);
        return error;
    }

    cnode_specs->caps[cnode_specs->end].base_addr = cnode_specs->caps[ut_idx].base_addr;
    cnode_specs->caps[cnode_specs->end].end_addr = cnode_specs->caps[ut_idx].base_addr + GET_OBJECT_SIZE(object_type, size_bits);
    cnode_specs->caps[cnode_specs->end].object_type = object_type;
    cnode_specs->caps[cnode_specs->end].is_device = cnode_specs->caps[ut_idx].is_device;
    cnode_specs->caps[cnode_specs->end].parent = ut_idx;
    cnode_specs->caps[cnode_specs->end].child = 0;
    cnode_specs->caps[cnode_specs->end].next = 0;
    cnode_specs->caps[ut_idx].base_addr = cnode_specs->caps[cnode_specs->end].end_addr;

    if (cnode_specs->caps[ut_idx].child == 0) {
        cnode_specs->caps[ut_idx].child = cnode_specs->end;
    } else {
        uint32_t child_ut_idx = cnode_specs->caps[ut_idx].child;

        while (cnode_specs->caps[child_ut_idx].next != 0) {
            child_ut_idx = cnode_specs->caps[child_ut_idx].next;
        }
        cnode_specs->caps[child_ut_idx].next = cnode_specs->end;
    }

    if (retyped_cap_idx != NULL) {
        *retyped_cap_idx = cnode_specs->end;
    }
    cnode_specs->end++;

    return seL4_NoError;
}