#pragma once
#include <microkit.h>
#include <lions/posix/pager_mem.h>

void allocator_init(void);

long pager_mem_call(microkit_msginfo msginfo, microkit_child child);