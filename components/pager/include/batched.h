#pragma once
#include <sel4/sel4.h>
#define MAX_BATCHED_MAPPINGS 58
#define MAX_BATCHED_UNMAPPINGS 119
typedef struct seL4_ARM_BatchedMapping_ {
    seL4_CPtr frame;
    seL4_Word vaddr;
} seL4_ARM_BatchedMapping;

typedef struct map_batch_ {
    seL4_ARM_BatchedMapping mappings[MAX_BATCHED_MAPPINGS];
    int count;
    seL4_Word rights;
    seL4_ARM_VMAttributes attr;
    bool is_active;
    seL4_CPtr vspace;
} map_batch;

static map_batch current_map = {.count = 0, .is_active = false};

/**
 * Begin a batched mapping operation. This function initializes the batch.
 * Required for add_batched_map.
 */
seL4_Error seL4_ARM_start_batched_mapping(seL4_CPtr vspace, seL4_Word rights, seL4_ARM_VMAttributes attr) {
    if ( current_map.is_active) {
        return seL4_InvalidOperation; // Already in a batched mapping operation
    }
    current_map.count = 0;
    current_map.is_active = true;
    current_map.rights = rights;
    current_map.attr = attr;
    current_map.vspace = vspace;
    return seL4_NoError;
}

// LIBSEL4_INLINE_FUNC seL4_Error seL4_ARM_VSpace_Map_Batched(seL4_ARM_VSpace _service,
//                                                            seL4_Word count,
//                                                            const seL4_ARM_BatchedMapping *mappings,
//                                                            seL4_CapRights_t rights,
//                                                            seL4_ARM_VMAttributes attr)
// {
//     if (count > seL4_MaxBatchedMap) {
//         return seL4_RangeError;
//     }

//     seL4_SetMR(0, count);
//     seL4_SetMR(1, rights.words[0]);
//     seL4_SetMR(2, attr);
//     for (seL4_Word i = 0; i < count; i++) {
//         seL4_SetMR(3 + 2 * i, mappings[i].frame);
//         seL4_SetMR(4 + 2 * i, mappings[i].vaddr);
//     }

//     seL4_MessageInfo_t tag = seL4_MessageInfo_new(ARMVSpaceMapBatched, 0, 0, 3 + 2 * count);
//     seL4_MessageInfo_t output_tag = seL4_Call(_service, tag);

//     return (seL4_Error) seL4_MessageInfo_get_label(output_tag);
// }

/**
 * Add mapping to current batch. Batch must be active.
 * Batch processed when count reaches MAX_BATCHED_MAPPINGS or when finish_batched_mapping is called.
 */
seL4_Error seL4_ARM_add_batched_map(seL4_CPtr frame, seL4_Word vaddr) {
    if (!current_map.is_active) {
        return seL4_InvalidOperation; // Not in a batched mapping operation
    }

    current_map.mappings[current_map.count].frame = frame;
    current_map.mappings[current_map.count].vaddr = vaddr;
    current_map.count++;

    if (current_map.count >= MAX_BATCHED_MAPPINGS) {
        seL4_Error err = seL4_ARM_VSpace_Map_Batched(current_map.vspace, current_map.count, current_map.mappings, current_map.rights, current_map.attr);
        if (err != seL4_NoError) {
            return err;
        }
        current_map.count = 0;
    }

    return seL4_NoError;
}

seL4_Error seL4_ARM_finish_batched_mapping(seL4_CPtr vspace, seL4_Word rights, seL4_ARM_VMAttributes attr) {
    if (current_map.count > 0) {
        seL4_Error err = seL4_ARM_VSpace_Map_Batched(current_map.vspace, current_map.count, current_map.mappings, current_map.rights, current_map.attr);
        if (err != seL4_NoError) {
            return err;
        }
        // Reset the batch after processing
        current_map.count = 0;
    }

    return seL4_NoError;
}

typedef struct unmap_batch_ {
    seL4_CPtr frames[MAX_BATCHED_UNMAPPINGS];
    int count;
    bool is_active;
} unmap_batch;

static unmap_batch current_unmap = {.count = 0, .is_active = false};

seL4_Error seL4_ARM_start_batched_unmapping() {
    if (current_map.is_active) {
        return seL4_InvalidOperation; // Already in a batched unmapping operation
    }
    current_unmap.count = 0;
    current_unmap.is_active = true;
    return seL4_NoError;
}

// LIBSEL4_INLINE_FUNC seL4_Error seL4_ARM_VSpace_Unmap_Batched(seL4_ARM_VSpace _service,
//                                                              seL4_Word count,
//                                                              const seL4_CPtr *frames)
// {
//     if (count > seL4_MaxBatchedUnmap) {
//         return seL4_RangeError;
//     }

//     seL4_SetMR(0, count);
//     for (seL4_Word i = 0; i < count; i++) {
//         seL4_SetMR(1 + i, frames[i]);
//     }

//     seL4_MessageInfo_t tag = seL4_MessageInfo_new(ARMVSpaceUnmapBatched, 0, 0, 1 + count);
//     seL4_MessageInfo_t output_tag = seL4_Call(_service, tag);

//     return (seL4_Error) seL4_MessageInfo_get_label(output_tag);
// }

seL4_Error seL4_ARM_add_batched_unmap(seL4_CPtr frame) {
    if (!current_unmap.is_active) {
        return seL4_InvalidOperation;
    }

    current_unmap.frames[current_unmap.count] = frame;
    current_unmap.count++;

    if (current_unmap.count >= MAX_BATCHED_UNMAPPINGS) {
        seL4_Error err = seL4_ARM_VSpace_Unmap_Batched(current_unmap.vspace, current_unmap.count, current_unmap.frames);
        if (err != seL4_NoError) {
            return err;
        }
        current_unmap.count = 0;
    }

    return seL4_NoError;
}

seL4_Error seL4_ARM_finish_batched_unmapping() {
    if (current_unmap.count > 0) {
        seL4_Error err = seL4_ARM_VSpace_Unmap_Batched(current_unmap.vspace, current_unmap.count, current_unmap.frames);
        if (err != seL4_NoError) {
            return err;
        }

        current_unmap.count = 0;
    }

    return seL4_NoError;
}

