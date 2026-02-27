// Very simple memory manager for the purpose of benchmarking.
// Malloc and free 4k frames.

#include <microkit.h>
#include <stdint.h>
#include "types.h"

// I should have a list of free and used 


// pool of mmap nodes
struct mmap_node node_pool[MAX_PDS][NUM_PT_ENTRIES]; 
struct mmap_node *used_nodes[MAX_PDS] = {NULL};
struct mmap_node *free_nodes[MAX_PDS] = {NULL};

/**
 * Allocates the next free 4K block.
 */
static int64_t malloc(microkit_child pd) {
    struct mmap_node *ptr = free_nodes[pd];
    free_nodes[pd] = ptr->next;
    ptr->next = used_nodes[pd];
    used_nodes[pd] = ptr;
    return ptr->addr;
}

/**
 * free the 4k block of memory which this addr is at.
 */
static int64_t free(uintptr_t addr, microkit_child pd) {
    struct mmap_node* ptr = node_pool[pd][INDEX_INTO_MMAP_ARRAY(addr)];
    // remove from used_nodes
    ptr->prev->next = ptr->next;
    // add to free_nodes
    ptr->next = free_nodes[pd];
    free_nodes[pd] = ptr;
}

void init(void)
{
    // for each theoretically existing PD, push nodes into free nodes LL.
    for (int i = 0; i < MAX_PDS; ++i) {
        for (int j = 0; j < NUM_PT_ENTRIES; ++j) {
            node[i][j].addr = j * 4096;
            node_pool[i][j].next = free_nodes[i];
            free_nodes[i] = &node_pool[i][j];
        }
    }
}



seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    uint32_t inst = seL4_GetMR(msginfo, 0);
    uint32_t pd = seL4_GetMR(msginfo, 1);
    // 0 is free, 1 is malloc
    if (inst) {
        uintptr_t addr = seL4_GetMR(msginfo, 2);
        free(addr, pd);
        return microkit_msginfo_new(0, 0);
    } else {
        return microkit_msginfo_new(0, malloc(pd));
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
}



