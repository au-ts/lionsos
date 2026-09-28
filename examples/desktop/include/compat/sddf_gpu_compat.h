/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Compatibility shim for the sDDF GPU virtualiser.
 *
 * sDDF commit b76617b ("Fixup ialloc implmenetation") removed the offset
 * support from the index allocator, but gpu/components/virt.c still calls
 * ialloc_init_with_offset() so that resource id 0 (which disables a scanout)
 * is never handed out. We get the same effect by reserving the first `offset`
 * indices up front. The allocator then hands out ids offset..size-1, i.e. one
 * fewer resource than before, while staying within the caller's idxlist.
 * Remove this once the virtualiser is ported upstream.
 */

#pragma once

#include <stdint.h>
#include <sddf/util/ialloc.h>

static inline void ialloc_init_with_offset(ialloc_t *ia, uint32_t *idxlist, uint32_t size, uint32_t offset)
{
    ialloc_init(ia, idxlist, size);
    for (uint32_t i = 0; i < offset; i++) {
        uint32_t reserved;
        int err = ialloc_alloc(ia, &reserved);
        assert(!err && reserved == i);
    }
}
