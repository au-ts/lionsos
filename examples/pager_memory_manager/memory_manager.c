// Very simple memory manager for the purpose of benchmarking.
// Malloc and free 4k frames.

#include <microkit.h>
#include <stdint.h>
#include "types.h"
#include <stdio.h>
#include <sddf/util/printf.h>

#define MM_MALLOC 1
#define MM_FREE   0

// Pool of mmap nodes
struct mmap_node node_pool[MAX_PDS][NUM_PT_ENTRIES];
struct mmap_node *used_nodes[MAX_PDS] = {NULL};
struct mmap_node *free_nodes[MAX_PDS] = {NULL};

/**
 * Allocates the next free 4K block.
 * Returns the address on success, or -1 on failure.
 */
static int64_t do_malloc(microkit_channel pd) {
    if (pd >= MAX_PDS) return -1;

    struct mmap_node *ptr = free_nodes[pd];
    if (!ptr) return -1;

    // Remove from free list
    free_nodes[pd] = ptr->next;
    if (free_nodes[pd]) {
        free_nodes[pd]->prev = NULL;
    }

    // Add to used list (insert at head)
    ptr->next = used_nodes[pd];
    ptr->prev = NULL;
    if (used_nodes[pd]) {
        used_nodes[pd]->prev = ptr;
    }
    used_nodes[pd] = ptr;

    return (int64_t)ptr->addr;
}

/**
 * Free the 4K block of memory at this addr.
 * Returns 0 on success, -1 on failure.
 */
static int do_free(uintptr_t addr, microkit_channel pd) {
    if (pd >= MAX_PDS) return -1;

    // Validate address is within range and aligned
    if (addr < BRK_START || addr & 0xFFF) return -1;

    uintptr_t index = INDEX_INTO_MMAP_ARRAY(addr);
    if (index >= NUM_PT_ENTRIES) return -1;

    struct mmap_node *ptr = &node_pool[pd][index];

    // Remove from used list
    if (used_nodes[pd] == ptr) {
        used_nodes[pd] = ptr->next;
        if (used_nodes[pd]) {
            used_nodes[pd]->prev = NULL;
        }
    } else {
        if (!ptr->prev) {
            // Node is not in the used list — double free or invalid
            return -1;
        }
        ptr->prev->next = ptr->next;
        if (ptr->next) {
            ptr->next->prev = ptr->prev;
        }
    }

    // Add to free list (insert at head)
    ptr->next = free_nodes[pd];
    ptr->prev = NULL;
    if (free_nodes[pd]) {
        free_nodes[pd]->prev = ptr;
    }
    free_nodes[pd] = ptr;

    return 0;
}

void init(void)
{
    sddf_dprintf("hello from memory manager\n");
    for (int i = 0; i < MAX_PDS; ++i) {
        free_nodes[i] = NULL;
        used_nodes[i] = NULL;
        for (int j = 0; j < NUM_PT_ENTRIES; ++j) {
            node_pool[i][j].addr = BRK_START + (uintptr_t)j * 4096;
            node_pool[i][j].prev = NULL;
            node_pool[i][j].next = free_nodes[i];

            if (free_nodes[i]) {
                free_nodes[i]->prev = &node_pool[i][j];
            }
            free_nodes[i] = &node_pool[i][j];
        }
    }
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    uint32_t inst = seL4_GetMR(0);

    if (inst == MM_MALLOC) {
        microkit_msginfo ret = microkit_msginfo_new(0, 1);
        seL4_SetMR(0, do_malloc(ch));
        return ret;
    } else if (inst == MM_FREE) {
        uintptr_t addr = seL4_GetMR(2);
        int result = do_free(addr, ch);
        return microkit_msginfo_new(0, 0);
    } else {
        sddf_printf("unknown instruction\n");
        return microkit_msginfo_new(0, 0);
    }
}

void notified(microkit_channel ch)
{
    // Not used
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    return seL4_False;
}