/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>
#include <bits/syscall.h>
#include <lions/posix/posix.h>
#include <lions/posix/pager_mem.h>
#include <microkit.h>

#ifdef __NR_clone
static long sys_clone(va_list ap) {
    unsigned long flags = va_arg(ap, unsigned long);
    void *child_stack = va_arg(ap, void *);
    void *ptid = va_arg(ap, void *);
    void *tls = va_arg(ap, void *);
    void *ctid = va_arg(ap, void *);
    microkit_mr_set(0, flags);
    microkit_mr_set(1, (uintptr_t)child_stack);
    microkit_mr_set(2, (uintptr_t)ptid);
    microkit_mr_set(3, (uintptr_t)tls);
    microkit_mr_set(4, (uintptr_t)ctid);
    (void)microkit_ppcall(PAGER_MEM_CH, microkit_msginfo_new(PAGER_MEM_FORK, 5));
    return microkit_mr_get(0);
}
#endif

#ifdef __NR_fork
static long sys_fork(va_list ap) {
    (void)ap;
    (void)microkit_ppcall(PAGER_MEM_CH, microkit_msginfo_new(PAGER_MEM_FORK, 0));
    return microkit_mr_get(0);
}
#endif

#ifdef __NR_vfork
static long sys_vfork(va_list ap) {
    (void)ap;
    (void)microkit_ppcall(PAGER_MEM_CH, microkit_msginfo_new(PAGER_MEM_FORK, 0));
    return microkit_mr_get(0);
}
#endif

#ifdef __NR_clone3
static long sys_clone3(va_list ap) {
    void *cl_args = va_arg(ap, void *);
    size_t size = va_arg(ap, size_t);
    microkit_mr_set(0, (uintptr_t)cl_args);
    microkit_mr_set(1, size);
    (void)microkit_ppcall(PAGER_MEM_CH, microkit_msginfo_new(PAGER_MEM_FORK, 2));
    return microkit_mr_get(0);
}
#endif

void libc_init_proc(void) {
#ifdef __NR_clone
    libc_define_syscall(__NR_clone, sys_clone);
#endif
#ifdef __NR_fork
    libc_define_syscall(__NR_fork, sys_fork);
#endif
#ifdef __NR_vfork
    libc_define_syscall(__NR_vfork, sys_vfork);
#endif
#ifdef __NR_clone3
    libc_define_syscall(__NR_clone3, sys_clone3);
#endif
}

