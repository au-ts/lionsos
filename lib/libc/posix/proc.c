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
#ifdef LIBC_PAGER
#include <lions/pager/config.h>
#include <lions/posix/pager.h>
#include <microkit.h>
#endif

/*
 * fork()/clone() are the pager's to implement -- it is the one that can copy a
 * vspace -- so without LIBC_PAGER there is nothing to forward them to and they
 * stay undefined, as they were before the pager component existed. libc_init()
 * always calls libc_init_proc(), so this file is always linked in.
 */
#ifdef LIBC_PAGER

/* Defined by the client PD in .pager_client_config. */
extern pager_client_config_t pager_config;

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
    (void)microkit_ppcall(pager_config.id, microkit_msginfo_new(PAGER_MEM_FORK, 5));
    return microkit_mr_get(0);
}
#endif

#ifdef __NR_fork
static long sys_fork(va_list ap) {
    (void)ap;
    (void)microkit_ppcall(pager_config.id, microkit_msginfo_new(PAGER_MEM_FORK, 0));
    return microkit_mr_get(0);
}
#endif

#endif /* LIBC_PAGER */

void libc_init_proc(void) {
#ifdef LIBC_PAGER
#ifdef __NR_clone
    libc_define_syscall(__NR_clone, sys_clone);
#endif
#ifdef __NR_fork
    libc_define_syscall(__NR_fork, sys_fork);
#endif
#endif /* LIBC_PAGER */
}

