/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdlib.h>

#include <lions/fs/protocol.h>
#include <liburing.h>

#include "op.h"

/* Checks whether the io_uring submission queue is empty */
bool io_uring_sqe_queue_empty(struct io_uring *ring);

/* Given a ring structure of io_uring. Flush the SQEs for processing, wait for them all
   to complete and invoke the callbacks. */
void flush_and_wait_io_uring_sqes(struct io_uring *ring, uint64_t *comp_idx);

/* Grab the private callback data from an io_uring's Completion Queue Entry (CQE) */
io_uring_comp_callback_t *cb_dat_from_cqe(struct io_uring_cqe *cqe);

/* Enqueue a reply into the completion queue, but not publish it! */
void fs_queue_enqueue_reply(fs_cmpl_t cmpl, uint64_t *comp_idx);

/* Convert a fs_buffer_t into our vaddr */
void *fs_get_buffer(fs_buffer_t buf);
/* Copy the path from client then concat it with the mount point.
   If the call succeeds, the path's total length will be written to *path_len.
   *** Returns a buffer from malloc. *** */
char *fs_malloc_create_path(fs_buffer_t path, size_t *path_len);

/* Custom optimised memcpy as the string.h's memcpy is not compatible with UIO mappings */
void fs_memcpy(char *dest, const char *src, size_t n);

/* Converts the errno into LionsOS' equivalent status. */
uint64_t errno_to_lions_status(int err_num);
