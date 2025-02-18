/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#include <lions/fs/protocol.h>
#include <uio/fs.h>
#include <liburing.h>

#include "util.h"
#include "log.h"
#include "op.h"

/* From main.c */
extern char mnt_point[PATH_MAX];
extern int mnt_point_len;
extern struct fs_queue *comp_queue;
extern char *fs_data;

bool io_uring_sqe_queue_empty(struct io_uring *ring) {
    struct io_uring_sq *sq = &ring->sq;
    return sq->khead == sq->ktail;
}

void flush_and_wait_io_uring_sqes(struct io_uring *ring, uint64_t *comp_idx) {
    /* Poke the Linux kernel. */

    int n_submitted = io_uring_submit(ring);
    if (n_submitted > 0) {
        /* Something was submitted, now wait. */
        struct io_uring_cqe *first_cqe;
        int wait_err = io_uring_wait_cqes(ring, &first_cqe, n_submitted, NULL, NULL);
        if (wait_err) {
            LOG_FS_ERR("flush_io_uring_sqes(): io_uring_wait_cqes(): failed: %s", strerror(-wait_err));
            exit(EXIT_FAILURE);
        }

        /* For each completion, invoke the callback then enqueue the reply */
        struct io_uring_cqe *this_cqe;
        unsigned head;
        unsigned i = 0;
        io_uring_for_each_cqe(ring, head, this_cqe) {
            callback_handler[cb_dat_from_cqe(this_cqe)->cmd_type](this_cqe, comp_idx);
            i += 1;
        }
        io_uring_cq_advance(ring, i);
        assert(i == n_submitted);
    } else if (n_submitted == 0) {
        /* No-op, only received mount/unmount */
    } else {
        LOG_FS_ERR("flush_io_uring_sqes(): io_uring_submit(): failed: %s", strerror(-n_submitted));
        exit(EXIT_FAILURE);
    }
}

io_uring_comp_callback_t *cb_dat_from_cqe(struct io_uring_cqe *cqe) {
    return (io_uring_comp_callback_t *) cqe->user_data;
}

void fs_queue_enqueue_reply(fs_cmpl_t cmpl, uint64_t *comp_idx) {
    assert(fs_queue_length_producer(comp_queue) != FS_QUEUE_CAPACITY);
    fs_queue_idx_empty(comp_queue, *comp_idx)->cmpl = cmpl;
    *comp_idx += 1;
}

void *fs_get_buffer(fs_buffer_t buf) {
    if (buf.offset >= UIO_LENGTH_FS_DATA
        || buf.size > UIO_LENGTH_FS_DATA - buf.offset
        || buf.size == 0) {
        return NULL;
    }
    return (void *)(fs_data + buf.offset);
}

char *fs_malloc_create_path(fs_buffer_t params_path, size_t *path_len) {
    size_t path_size = mnt_point_len + params_path.size + 2; // extra slash and terminator

    char *path = malloc(path_size);
    if (!path) {
        return NULL;
    }

    strcpy(path, mnt_point);
    strcat(path, "/");
    strncat(path, fs_get_buffer(params_path), params_path.size);

    *path_len = path_size;
    return path;
}

void fs_memcpy(char *dest, const char *src, size_t n) {
    size_t i = 0;

    size_t odd_bytes = n % 8;
    while (i < odd_bytes) {
        dest[i] = src[i];
        i += 1;
    }

    size_t n_64bytes_chunks = n / 8;
    const char *remainder_src = &src[i];
    char *remainder_dest = &dest[i];

    for (size_t i = 0; i < n_64bytes_chunks; i++) {
        ((uint64_t *) remainder_dest)[i] = ((uint64_t *) remainder_src)[i];
    }

    assert(i == n);
}

uint64_t errno_to_lions_status(int err_num) {
    switch (errno) {
        case ENOENT: {
            return FS_STATUS_INVALID_PATH;
        }
        case ENOSPC:
        case EACCES: {
            return FS_STATUS_SERVER_WAS_DENIED;
        }
        case EROFS:
        case EBADF: {
            return FS_STATUS_INVALID_FD;
        }
        case EFAULT: {
            return FS_STATUS_INVALID_BUFFER;
        }
        case EMFILE:
        case ENFILE: {
            return FS_STATUS_TOO_MANY_OPEN_FILES;
        }
        case ENOMEM: {
            return FS_STATUS_ALLOCATION_ERROR;
        }
        case EDQUOT:
        case EOVERFLOW:
        case ENAMETOOLONG: {
            return FS_STATUS_INVALID_NAME;
        }
        case EBUSY: {
            return FS_STATUS_OUTSTANDING_OPERATIONS;
        }
        default:
            return FS_STATUS_ERROR;
    }
}
