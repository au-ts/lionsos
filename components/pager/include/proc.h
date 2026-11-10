/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _PROC_H
#define _PROC_H

#include <sel4/sel4.h>
#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>

#include "pager.h"

#define PROCESS_CSPACE_SIZE_BITS 12
#define PROCESS_CSPACE_GUARD_SIZE (seL4_WordBits - PROCESS_CSPACE_SIZE_BITS)
/* Slots of a process' own CSpace, at the numbers libmicrokit's INPUT_CAP and REPLY_CAP use. */
#define PROCESS_INPUT_SLOT 1
#define PROCESS_FAULT_EP_SLOT 2
#define PROCESS_REPLY_SLOT 4
#define PROCESS_PPC_EP_SLOT(client_id) (BASE_ENDPOINT_CAP + (client_id))

/*
 * Each process owns a fixed block of the process_cspaces CNode holding what the pager
 * invokes on its behalf. A cap inside the process' own CSpace cannot be named from the
 * pager's, so anything the pager passes as a cptr lives here. Slot 0 of the CNode is
 * its self-reference, so the first block starts one block in.
 */
#define PROCESS_BLOCK_SLOTS 8
#define PROCESS_BLOCK(pid) (PROCESS_BLOCK_SLOTS * ((pid) + 1))
#define PROCESS_BLOCK_CSPACE 0
#define PROCESS_BLOCK_TCB 1
#define PROCESS_BLOCK_SC 2
/* The unbadged original of the process' notification. */
#define PROCESS_BLOCK_NTFN 3
/* The process' badged fault endpoint. On MCS seL4_TCB_SetSchedParams looks it up in the
 * caller's CSpace, so the copy in the process' own CSpace cannot be used for it. */
#define PROCESS_BLOCK_FAULT_EP 4

/* Matches the scheduling contexts the Microkit tool creates for PDs. */
#define PROCESS_SC_SIZE_BITS 8
/* Badges the Microkit tool mints endpoint caps with; see its capdl/builder.rs. */
#define MICROKIT_FAULT_BADGE (1ULL << 62)
#define MICROKIT_PPC_BADGE (1ULL << 63)

#define PROCESS_FORK_OK 0
#define PROCESS_FORK_INVALID -1
#define PROCESS_FORK_NO_SLOTS -2
#define PROCESS_FORK_CAP -3
#define PROCESS_FORK_NO_SCHED_CONTROL -4
#define PROCESS_FORK_START -5

#define STACK_LOCATION 0x7fffffffe000
#define STACK_PAGES 2
/* Where libmicrokit expects its IPC buffer: the top page of the user address space, which
 * depends on whether seL4 runs at EL2. */
#define IPC_LOCATION (seL4_UserVSpaceTop & ~((1ULL << seL4_PageBits) - 1))

struct process {
    seL4_CPtr vspace;
    seL4_CPtr cspace;
    seL4_CPtr ipc_buffer;
    /* For a client the system booted with, the cap the Microkit tool gave us. */
    seL4_CPtr tcb;
    seL4_CPtr sched_context;
    seL4_CPtr ntfn;
    seL4_CPtr fault_ep;
    uint32_t parent_pid;
    uint32_t pid;
    /* Scheduling parameters, inherited from the parent. budget and period are in us. */
    uint64_t budget;
    uint64_t period;
    uint8_t priority;
    /* The client the system booted with that this process descends from. The process runs
     * its code against a copy of its configs, so has its channels and mappings. */
    uint8_t template_client;
    /* The CSpace slot this process' libc PPCs the pager on, inherited by its children. */
    uint8_t ppc_ep_slot;
    bool allocated;
};

/**
 * Claims the CNode the per-process CSpaces are created in and registers the clients
 * the system booted with, one process per client.
 */
void process_init(seL4_CPtr process_cnode, uint8_t process_cnode_size_bits,
                  uint8_t num_clients);

int process_fork(uint32_t parent, uint32_t child);

/**
 * Forks parent into the first free process slot, returning the new child id.
 */
long pager_fork(microkit_child parent);
void fork(uint32_t parent, uint32_t child);

#endif
