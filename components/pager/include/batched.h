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

static map_batch current_batch = {.count = 0, .is_active = false};

/**
 * Begin a batched mapping operation. This function initializes the batch.
 * Required for add_batched_map.
 */
seL4_Error seL4_ARM_start_batched_mapping(seL4_CPtr vspace, seL4_Word rights, seL4_ARM_VMAttributes attr) {
    if ( current_batch.is_active) {
        return seL4_InvalidOperation; // Already in a batched mapping operation
    }
    current_batch.count = 0;
    current_batch.is_active = true;
    current_batch.rights = rights;
    current_batch.attr = attr;
    current_batch.vspace = vspace;
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
    if (!current_batch.is_active) {
        return seL4_InvalidOperation; // Not in a batched mapping operation
    }

    current_batch.mappings[current_batch.count].frame = frame;
    current_batch.mappings[current_batch.count].vaddr = vaddr;
    current_batch.count++;

    if (current_batch.count >= MAX_BATCHED_MAPPINGS) {
        seL4_Error err = seL4_ARM_VSpace_Map_Batched(current_batch.vspace, current_batch.count, current_batch.mappings, current_batch.rights, current_batch.attr);
        if (err != seL4_NoError) {
            return err;
        }
        current_batch.count = 0;
    }

    return seL4_NoError;
}

seL4_Error seL4_ARM_finish_batched_mapping(seL4_CPtr vspace, seL4_Word rights, seL4_ARM_VMAttributes attr) {
    if (current_batch.count > 0) {
        // Perform the actual mapping operation here for any remaining mappings
        // This is a placeholder for the actual mapping logic
        // You would typically call the underlying seL4 mapping functions here
        seL4_Error err = seL4_ARM_VSpace_Map_Batched(current_batch.vspace, current_batch.count, current_batch.mappings, current_batch.rights, current_batch.attr);
        if (err != seL4_NoError) {
            return err;
        }
        // Reset the batch after processing
        current_batch.count = 0;
    }

    return seL4_NoError;
}

typedef struct unmap_batch_ {
    seL4_CPtr frames[MAX_BATCHED_UNMAPPINGS];
    int count;
    bool is_active;
} unmap_batch;

seL4_Error seL4_ARM_start_batched_unmapping() {
    if (current_batch.is_active) {
        return seL4_InvalidOperation; // Already in a batched unmapping operation
    }
    current_batch.count = 0;
    current_batch.is_active = true;
    return seL4_NoError;
}

seL4_Error seL4_ARM_add_batched_unmap(seL4_CPtr frame) {
    if (!current_batch.is_active) {
        return seL4_InvalidOperation; // Not in a batched unmapping operation
    }

    current_batch.mappings[current_batch.count].frame = frame;
    current_batch.count++;

    if (current_batch.count >= MAX_BATCHED_UNMAPPINGS) {
        seL4_Error err = seL4_ARM_VSpace_Unmap_Batched(current_batch.vspace, current_batch.count, current_batch.frames);
        if (err != seL4_NoError) {
            return err;
        }
        current_batch.count = 0;
    }

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

seL4_Error seL4_ARM_finish_batched_unmapping() {
    if (current_batch.count > 0) {
        seL4_Error err = seL4_ARM_VSpace_Unmap_Batched(current_batch.vspace, current_batch.count, current_batch.frames);
        if (err != seL4_NoError) {
            return err;
        }
        // Reset the batch after processing
        current_batch.count = 0;
    }

    return seL4_NoError;
}

