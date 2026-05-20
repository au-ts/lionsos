/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <lions/posix/posix.h>

#define PAGE_SIZE 0x1000

static char *morecore_area;
static uintptr_t morecore_base;
static uintptr_t morecore_top;

/* Actual morecore implementation
   On Linux, the brk syscall returns the current break on failure. We mimic
   this behaviour here because we are using muslc which expects Linux behaviour.
*/
static long sys_brk(va_list ap) {
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    if (newbrk <= morecore_top && newbrk >= (uintptr_t)morecore_area) {
        return morecore_base = newbrk;
    }

    return morecore_base;
}

static long sys_mmap(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    (void)fd, (void)offset, (void)prot, (void)addr;

    if (length == 0) {
        return -EINVAL;
    }

    if (flags & MAP_ANONYMOUS) {
        /* Align length to page size */
        length = (length + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
        /* Check that we don't try and allocate more than exists */
        if (length > morecore_top - morecore_base) {
            return -ENOMEM;
        }
        /* Steal from the top */
        morecore_top = (morecore_top - length) & ~(PAGE_SIZE - 1);
        return morecore_top;
    }
    return -ENOMEM;
}

static long sys_munmap(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t len = va_arg(ap, size_t);
    (void)addr, (void)len;

    return 0;
}

static long sys_mprotect(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t size = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    (void)addr, (void)size, (void)prot;

    return 0;
}

void libc_init_mem(void *area, size_t size) {
    assert(area != NULL && size != 0);

    morecore_area = area;
    morecore_base = (uintptr_t)area;
    morecore_top = morecore_base + size;

    libc_define_syscall(__NR_brk, sys_brk);
    libc_define_syscall(__NR_mmap, sys_mmap);
    libc_define_syscall(__NR_munmap, sys_munmap);
    libc_define_syscall(__NR_mprotect, sys_mprotect);
}
