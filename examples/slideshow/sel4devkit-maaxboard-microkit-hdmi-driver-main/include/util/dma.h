/* This work is Crown Copyright NCSC, 2024. */

#ifndef __DMA_H__
#define __DMA_H__

#include <stddef.h>
#include <stdint.h>

// Initialise the internal pointers to the physical and virtual memory as well as set the limit for the memory buffer
void sel4_dma_init(uintptr_t pbase, uintptr_t vbase, uintptr_t limit);

// Allocate memory and return the start address
uintptr_t* sel4_dma_alloc(size_t size);

// Returns the phyiscal address - This works by passing in the current virtual pointer to calculate what the offset is from the virtual base.
// This offset will be the same for the phyiscal memory, so it just returns the physical base plus the offset.
uintptr_t* getPhys(void* virt);

// Returns the virtual address (Same as above but for virtual address)
uintptr_t* getVirt(void* paddr);

#endif