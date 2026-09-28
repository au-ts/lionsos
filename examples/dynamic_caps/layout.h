/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Where the manager puts things in the sandbox, shared by manager.c and program.c */

#pragma once

#define PROGRAM_VADDR 0x80000000UL
#define PROGRAM_MAX_PAGES 4
#define PROGRAM_STACK_VADDR 0x80010000UL
#define PROGRAM_STACK_TOP (PROGRAM_STACK_VADDR + 0x1000)
#define PROGRAM_SHARED_VADDR 0x80020000UL

/* The slot of the sandbox's root CNode holding the notification it is granted */
#define SANDBOX_NTFN_SLOT 7
/* Its CPtr from inside the sandbox: root slot << (64 - PD_ROOT_CAP_BITS) */
#define SANDBOX_NTFN_CPTR ((seL4_CPtr)SANDBOX_NTFN_SLOT << 58)
