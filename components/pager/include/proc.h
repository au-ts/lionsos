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
#define PROCESS_VSPACE_SLOT 1

#define PROCESS_FORK_OK 0
#define PROCESS_FORK_INVALID -1
#define PROCESS_FORK_NO_SLOTS -2
#define PROCESS_FORK_CAP -3

struct process {
    seL4_CPtr vspace;
    seL4_CPtr cspace;
    seL4_CPtr ipc_buffer;
    seL4_CPtr tcb;
    seL4_CPtr sched_context;
    seL4_Word asid_pool;
    seL4_Word asid;
    uint32_t parent_pid;
    uint32_t pid;
    bool allocated;
};

/**
 * Claims the CNode the per-process CSpaces are created in and registers the clients
 * the system booted with, one process per client.
 */
void process_init(seL4_CPtr process_cnode, uint8_t num_clients);

int process_fork(uint32_t parent, uint32_t child);

/**
 * Forks parent into the first free process slot, returning the new child id.
 */
long pager_fork(microkit_child parent);
void fork(uint32_t parent, uint32_t child);

// create vspace, then assign asid pool to vspace
// create cspace
// create IPC buffer
// create a TCB and configure it.
// create scheduling context
// set scheduling params.

#endif
