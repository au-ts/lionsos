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
#include <lions/posix/pager_mem.h>
#include <microkit.h>

#define PAGE_SIZE 0x1000

static long sys_brk(va_list ap) {
    uintptr_t newbrk = va_arg(ap, uintptr_t);
    microkit_mr_set(0, newbrk);
    (void)microkit_ppcall(PAGER_MEM_CH, microkit_msginfo_new(PAGER_MEM_BRK, 1));
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
    (void)microkit_ppcall(PAGER_MEM_CH, microkit_msginfo_new(PAGER_MEM_MMAP, 6));
    return microkit_mr_get(0);
}

static long sys_munmap(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t len = va_arg(ap, size_t);
    microkit_mr_set(0, (uintptr_t)addr);
    microkit_mr_set(1, len);
    (void)microkit_ppcall(PAGER_MEM_CH, microkit_msginfo_new(PAGER_MEM_MUNMAP, 2));
    return microkit_mr_get(0);
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

    (void)area;
    (void)size;

    libc_define_syscall(__NR_brk, sys_brk);
    libc_define_syscall(__NR_mmap, sys_mmap);
    libc_define_syscall(__NR_munmap, sys_munmap);
    libc_define_syscall(__NR_mprotect, sys_mprotect);
}
