// Very simple memory manager for the purpose of benchmarking.
// Malloc and free 4k frames.

#include <microkit.h>
#include <stdint.h>
#include "types.h"
#include <stdio.h>
#include <sddf/util/printf.h>

// I should have a list of free and used 

// pool of mmap nodes
struct mmap_node node_pool[MAX_PDS][NUM_PT_ENTRIES]; 
struct mmap_node *used_nodes[MAX_PDS] = {NULL};
struct mmap_node *free_nodes[MAX_PDS] = {NULL};

/**
 * Allocates the next free 4K block.
 */
static int64_t do_malloc(microkit_channel pd) {
    // if (pd > 63)  {
    //     struct mmap_node *thing = free_nodes[pd];
    //     return -1;
    // }
    struct mmap_node *ptr = free_nodes[pd];
    if (!ptr) return -1;
    free_nodes[pd] = ptr->next;
    ptr->next = used_nodes[pd];
    if (used_nodes[pd]) {
        used_nodes[pd]->prev = ptr;
    }
    used_nodes[pd] = ptr;
    if (free_nodes[pd]) free_nodes[pd]->prev = NULL;
    ptr->prev = NULL; // this might be unecessary.
    return ptr->addr;
}

/**
 * free the 4k block of memory which this addr is at.
 */
static void do_free(uintptr_t addr, microkit_channel pd) {
    struct mmap_node* ptr = &node_pool[pd][INDEX_INTO_MMAP_ARRAY(addr)];
    // remove from used_nodes
    // sddf_printf("\n\nThe channel PD is %d\n\n", pd);
    if (used_nodes[pd] == ptr) {
        used_nodes[pd] = ptr->next;
        if (used_nodes[pd]) used_nodes[pd]->prev = NULL; // this might be unecessary.
    } else {
        ptr->prev->next = ptr->next;
        if (ptr->next) {
            ptr->next->prev = ptr->prev;
        }
    }
    
    // add to free_nodes
    ptr->next = free_nodes[pd];
    free_nodes[pd] = ptr;
    ptr->prev = NULL; // this may be unecessary.

}

void init(void)
{
    sddf_dprintf("hello \n");
    // for each theoretically existing PD, push nodes into free nodes LL.
    for (int i = 0; i < MAX_PDS; ++i) {
        for (int j = 0; j < NUM_PT_ENTRIES; ++j) {
            node_pool[i][j].addr = BRK_START + j * 4096;
            node_pool[i][j].next = free_nodes[i];
            node_pool[i][j].prev = NULL; // This might be unecessary.

            if (free_nodes[i]) {
                free_nodes[i]->prev = &node_pool[i][j];   // 🔥 THIS IS THE MISSING LINE
            }
            free_nodes[i] = &node_pool[i][j];
        }
    }
}


/**
 * TODOS: 
 * - check if the way that I am doing the MR stuff is correct.
 */
seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    uint32_t inst = seL4_GetMR(0);
    // 0 is free, 1 is malloc
    if (inst) {
        return microkit_msginfo_new(0, do_malloc(ch));
    } else {
        uintptr_t addr = seL4_GetMR(2);
        do_free(addr, ch);
        return microkit_msginfo_new(0, 0);
    }
    
}

// NOT USED BELOW:

void notified(microkit_channel ch)
{
    // this may not be required
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // not required.
    return seL4_False;
}



