/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * WebAssembly apps in seL4 sandboxes (build with SANDBOX=1).
 *
 * The host PD stays the broker: it reads apps and their .caps files from the
 * disk, applies the policy and shows the list. There are GUI_WASM_WINDOWS
 * sandboxes, child PDs of the host, each paired with a window slot of the
 * compositor, so as many apps can run at once, each in a window of its own.
 * To run an app, the host picks a free sandbox and builds a fresh address
 * space for it from that sandbox's Untyped: the runner (WAMR and runner.c),
 * a stack, a heap, the module, and one page for each of the grants and the
 * mailbox. What the app is granted decides what else is mapped:
 *
 *   window   the frames of the surface and state of the sandbox's window
 *   file X   a read-only copy of that file, and nothing else from the disk
 *
 * so an app without them has no way to reach them: the memory is not in its
 * address space. The console and the timer are services of the host, which
 * checks the grant on every request, as any seL4 server checks its
 * clients. Stopping the app revokes the Untyped, which takes everything
 * away at once, and deletes the host's copies of the window's frame caps.
 *
 * The sandbox's only static capabilities are its Microkit channel to the
 * host, over which the two signal each other, and its own TCB and
 * scheduling context; everything else is made at run time.
 */

#pragma once

#include <stdint.h>
#include <lions/gui/protocol.h>
#include "caps.h"

/*
 * The host's channel to sandbox k, whose id as a child of the host is k, and
 * the sandbox's to the host. The host speaks to the compositor for the
 * window of sandbox k on channel SANDBOX_WINDOW_CH_BASE + k. See meta.py.
 */
#define SANDBOX_HOST_CH_BASE 50
#define SANDBOX_WINDOW_CH_BASE 40
#define SANDBOX_RUNNER_CH 0

/* The layout of the sandbox's address space. The runner is linked at SANDBOX_RUNNER_VADDR. */
#define SANDBOX_RUNNER_VADDR 0x10000000UL
#define SANDBOX_RUNNER_MAX 0x200000UL
#define SANDBOX_STACK_VADDR 0x30000000UL
#define SANDBOX_STACK_SIZE 0x20000UL
#define SANDBOX_STACK_TOP (SANDBOX_STACK_VADDR + SANDBOX_STACK_SIZE)
#define SANDBOX_HEAP_VADDR 0x31000000UL
#define SANDBOX_HEAP_SIZE 0x300000UL
#define SANDBOX_MODULE_VADDR 0x32000000UL
#define SANDBOX_MODULE_MAX 0x80000UL
#define SANDBOX_GRANTS_VADDR 0x33000000UL
#define SANDBOX_MAILBOX_VADDR 0x33010000UL
#define SANDBOX_SURFACE_VADDR 0x34000000UL
#define SANDBOX_STATE_VADDR 0x35000000UL
#define SANDBOX_FILES_VADDR 0x36000000UL
/* Each granted file gets this much address space */
#define SANDBOX_FILE_MAX 0x100000UL

#define SANDBOX_PAGE_SIZE 0x1000UL

#define SANDBOX_GRANTS_MAGIC 0x53424758 /* "SBGX" */
#define SANDBOX_MAX_GRANTS 8
#define SANDBOX_NAME_MAX 64

/* Types of grant, as cap_type_t in caps.h */
#define SANDBOX_GRANT_WINDOW 1
#define SANDBOX_GRANT_TIMER 2
#define SANDBOX_GRANT_CONSOLE 3
#define SANDBOX_GRANT_FILE 4

typedef struct sandbox_grant {
    uint32_t type;
    uint32_t live;
    char name[SANDBOX_NAME_MAX];
    /* For files: where the contents are mapped, and their size */
    uint64_t vaddr;
    uint64_t size;
} sandbox_grant_t;

/*
 * What the app was granted, written by the host and mapped read-only in the
 * sandbox. The index of a grant is the app's handle for it, as in caps.h.
 */
typedef struct sandbox_grants {
    uint32_t magic;
    uint32_t module_size;
    /* The window's size, 0 without a window */
    uint32_t width;
    uint32_t height;
    char app_name[SANDBOX_NAME_MAX];
    sandbox_grant_t grants[SANDBOX_MAX_GRANTS];
} sandbox_grants_t;

/* Messages from the host to the runner */
#define SANDBOX_MSG_EVENT 1 /* ev; for keys, ev.x is the typed character */
#define SANDBOX_MSG_TICK 2  /* time_ms */

typedef struct sandbox_msg {
    uint32_t type;
    uint32_t time_ms;
    gui_event_t ev;
} sandbox_msg_t;

/* Requests from the runner to the host */
#define SANDBOX_REQ_COMMIT 1 /* the app committed to its window */
#define SANDBOX_REQ_LOG 2    /* text: a line for the console */
#define SANDBOX_REQ_TIMER 3  /* arg: tick interval in ms, 0 to stop */
#define SANDBOX_REQ_DROP 4   /* arg: handle of a capability the app gave up */
#define SANDBOX_REQ_EXIT 5   /* the app ended itself */
#define SANDBOX_REQ_TRAP 6   /* text: why the app stopped */
#define SANDBOX_REQ_DEBUG 7  /* text: output of the runtime itself */
#define SANDBOX_REQ_DENIED 8 /* arg: handle, type: the grant type needed, text: the operation */

#define SANDBOX_TEXT_MAX 120

typedef struct sandbox_req {
    uint32_t type;
    int32_t arg;
    uint32_t grant_type;
    uint32_t _reserved;
    char text[SANDBOX_TEXT_MAX];
} sandbox_req_t;

#define SANDBOX_MSGS 64
#define SANDBOX_REQS 16

/*
 * One page shared by the host and the runner, with a ring in each
 * direction. Each index is written by one side only. The host treats
 * everything in it as untrusted: it validates indices, types and text.
 */
typedef struct sandbox_mailbox {
    uint32_t msg_head; /* written by the runner */
    uint32_t msg_tail; /* written by the host */
    uint32_t req_head; /* written by the host */
    uint32_t req_tail; /* written by the runner */
    uint32_t reqs_dropped;
    uint32_t _reserved[3];
    sandbox_msg_t msgs[SANDBOX_MSGS];
    sandbox_req_t reqs[SANDBOX_REQS];
} sandbox_mailbox_t;

_Static_assert(sizeof(sandbox_mailbox_t) <= SANDBOX_PAGE_SIZE, "the mailbox must fit in a page");
_Static_assert(sizeof(sandbox_grants_t) <= SANDBOX_PAGE_SIZE, "the grants must fit in a page");

/* A handle is an index into the host's caps table (caps.h), into the grants page
   above, and into the frame ranges the host recorded by that same index, which
   sandbox_revoke_grant() takes apart on cap_drop. If the table grew past the
   page those indices would stop agreeing, and a dropped file would unmap the
   wrong one. */
_Static_assert(CAPS_MAX <= SANDBOX_MAX_GRANTS, "the caps table and the grants page must be indexed alike");

/* Grants are described in one page and the mailbox is the next, so the two must
   not run into each other. */
_Static_assert(SANDBOX_GRANTS_VADDR + SANDBOX_PAGE_SIZE <= SANDBOX_MAILBOX_VADDR,
               "the grants page must not reach into the mailbox");
