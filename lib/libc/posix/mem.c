/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <errno.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <lions/posix/posix.h>

#define PAGE_SIZE 0x1000

/*
 * Statically allocated morecore area.
 *
 * This is rather terrible, but is the simplest option without a
 * huge amount of infrastructure.
 */
#define MORECORE_AREA_BYTE_SIZE 0x100000
static alignas(8) char morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Pointer to free space in the morecore area. */
static uintptr_t morecore_base = (uintptr_t)&morecore_area;
static uintptr_t morecore_top = (uintptr_t)&morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Actual morecore implementation
   On Linux, the brk syscall returns the current break on failure. We mimic
   this behaviour here because we are using muslc which expects Linux behaviour.
*/
static long sys_brk(va_list ap) {
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    if (newbrk <= morecore_top && newbrk >= (uintptr_t)&morecore_area[0]) {
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

void libc_init_mem() {
    libc_define_syscall(__NR_brk, sys_brk);
    libc_define_syscall(__NR_mmap, sys_mmap);
    libc_define_syscall(__NR_munmap, sys_munmap);
    libc_define_syscall(__NR_mprotect, sys_mprotect);
}
