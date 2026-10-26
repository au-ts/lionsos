/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * brk/mmap for musl, in two flavours picked at compile time.
 *
 * LIBC_PAGER defined: the client has a pager PD as its fault handler, and
 * these become PPCs to it, so the heap is demand-paged out of the pager's
 * untypeds and libc_init()'s area/size are ignored -- the arena is whatever
 * the client passed as pager_config.mmap_base. Set LIBC_PAGER := 1 before
 * including libc.mk to get this.
 *
 * LIBC_PAGER undefined (the default): a morecore allocator over the static
 * area the client hands to libc_init(). No pager, no PPC, no
 * .pager_client_config section -- which is what every client that is not
 * paged needs, since libc_init() always calls libc_init_mem() and so always
 * pulls this file in.
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <lions/posix/posix.h>
#ifdef LIBC_PAGER
#include <lions/pager/config.h>
#include <lions/posix/pager.h>
#include <microkit.h>
#endif

#define PAGE_SIZE 0x1000

#ifdef LIBC_PAGER

/* Defined by the client PD in .pager_client_config. */
extern pager_client_config_t pager_config;

static long sys_brk(va_list ap) {
    uintptr_t newbrk = va_arg(ap, uintptr_t);
    microkit_mr_set(0, newbrk);
    (void)microkit_ppcall(pager_config.id, microkit_msginfo_new(PAGER_MEM_BRK, 1));
    return microkit_mr_get(0);
}

static long sys_mmap(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    microkit_mr_set(0, (uintptr_t)addr);
    microkit_mr_set(1, length);
    microkit_mr_set(2, (uintptr_t)prot);
    microkit_mr_set(3, (uintptr_t)flags);
    microkit_mr_set(4, (uintptr_t)fd);
    microkit_mr_set(5, (uintptr_t)offset);
    (void)microkit_ppcall(pager_config.id, microkit_msginfo_new(PAGER_MEM_MMAP, 6));
    return microkit_mr_get(0);
}

static long sys_munmap(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t len = va_arg(ap, size_t);
    microkit_mr_set(0, (uintptr_t)addr);
    microkit_mr_set(1, len);
    (void)microkit_ppcall(pager_config.id, microkit_msginfo_new(PAGER_MEM_MUNMAP, 2));
    return microkit_mr_get(0);
}

#else /* !LIBC_PAGER */

static char *morecore_area;
static uintptr_t morecore_base;
static uintptr_t morecore_top;

/* On Linux the brk syscall returns the current break on failure. We mimic that
   here because muslc expects Linux behaviour. */
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

#endif /* LIBC_PAGER */

static long sys_mprotect(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t size = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    (void)addr, (void)size, (void)prot;

    return 0;
}

void libc_init_mem(void *area, size_t size) {
    assert(area != NULL && size != 0);

#ifdef LIBC_PAGER
    (void)area;
    (void)size;
#else
    morecore_area = area;
    morecore_base = (uintptr_t)area;
    morecore_top = morecore_base + size;
#endif

    libc_define_syscall(__NR_brk, sys_brk);
    libc_define_syscall(__NR_mmap, sys_mmap);
    libc_define_syscall(__NR_munmap, sys_munmap);
    libc_define_syscall(__NR_mprotect, sys_mprotect);
}
