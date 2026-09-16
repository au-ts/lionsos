
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "cspace.h"

#define PAGE_OFFSET(vaddr) (vaddr & 0xFFF)
#define PAGE_SIZE GET_OBJECT_SIZE(seL4_X86_4K, 0)

bool map_memory_region(cnode_specs_t *cnode_specs, uintptr_t paddr, uintptr_t size, uintptr_t vaddr);
seL4_Error retype_and_map_frame(cnode_specs_t *cnode_specs, uintptr_t paddr, uintptr_t vaddr, seL4_CPtr vspace, seL4_Word page_type, seL4_CapRights_t rights);
