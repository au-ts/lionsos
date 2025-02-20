/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <liburing.h>
#include <lions/fs/protocol.h>

/* Dispatch functions, modifies comp_idx if it sent a reply */
/* The ones below are asynchronous and uses io_uring. */
void handle_open(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_stat(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_fsize(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_close(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_read(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_write(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_rename(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_unlink(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_truncate(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_fsync(fs_cmd_t cmd, uint64_t *comp_idx);
/* The ones below are synchronous due to a lack of corresponding operation in io_uring */
void handle_initialise(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_deinitialise(fs_cmd_t cmd, uint64_t *comp_idx);
/* Calling mkdir and rmdir will cause the io_uring SQEs queue to flush,
   acting as a barrier to maintain order of operations for correctness. */
void handle_mkdir(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_rmdir(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_opendir(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_closedir(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_readdir(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_seekdir(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_telldir(fs_cmd_t cmd, uint64_t *comp_idx);
void handle_rewinddir(fs_cmd_t cmd, uint64_t *comp_idx);

static void (*const cmd_handler[FS_NUM_COMMANDS])(fs_cmd_t cmd, uint64_t *comp_idx) = {
    [FS_CMD_INITIALISE] = handle_initialise,
    [FS_CMD_DEINITIALISE] = handle_deinitialise,
    [FS_CMD_FILE_OPEN] = handle_open,
    [FS_CMD_FILE_CLOSE] = handle_close,
    [FS_CMD_STAT] = handle_stat,
    [FS_CMD_FILE_READ] = handle_read,
    [FS_CMD_FILE_WRITE] = handle_write,
    [FS_CMD_FILE_SIZE] = handle_fsize,
    [FS_CMD_RENAME] = handle_rename,
    [FS_CMD_FILE_REMOVE] = handle_unlink,
    [FS_CMD_FILE_TRUNCATE] = handle_truncate,
    [FS_CMD_DIR_CREATE] = handle_mkdir,
    [FS_CMD_DIR_REMOVE] = handle_rmdir,
    [FS_CMD_DIR_OPEN] = handle_opendir,
    [FS_CMD_DIR_CLOSE] = handle_closedir,
    [FS_CMD_FILE_SYNC] = handle_fsync,
    [FS_CMD_DIR_READ] = handle_readdir,
    [FS_CMD_DIR_SEEK] = handle_seekdir,
    [FS_CMD_DIR_TELL] = handle_telldir,
    [FS_CMD_DIR_REWIND] = handle_rewinddir,
};

/* Data structures to construct a "callback" from an io_uring completion. */
/* This is malloc'ed! */
typedef struct io_uring_comp_callback {
    uint64_t cmd_id;
    uint64_t cmd_type;
    fs_buffer_t resp_buf;

    /* Buffers malloced by a dispatch function.
       Must be free'ed if the value is not NULL */
    char *malloced_data_1;
    char *malloced_data_2;
} io_uring_comp_callback_t;

/* Callback functions */
void cb_open(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_stat(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_fsize(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_close(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_read(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_write(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_rename(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_unlink(struct io_uring_cqe *cqe, uint64_t *comp_idx);
void cb_fsync(struct io_uring_cqe *cqe, uint64_t *comp_idx);

// No-ops
// void cb_initialise(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_deinitialise(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_truncate(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_mkdir(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_rmdir(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_opendir(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_closedir(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_readdir(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_seekdir(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_telldir(struct io_uring_cqe *cqe, uint64_t *comp_idx);
// void cb_rewinddir(struct io_uring_cqe *cqe, uint64_t *comp_idx);

static void (*const callback_handler[FS_NUM_COMMANDS])(struct io_uring_cqe *cqe, uint64_t *comp_idx) = {
    [FS_CMD_INITIALISE] = NULL,
    [FS_CMD_DEINITIALISE] = NULL,
    [FS_CMD_FILE_OPEN] = cb_open,
    [FS_CMD_FILE_CLOSE] = cb_close,
    [FS_CMD_STAT] = cb_stat,
    [FS_CMD_FILE_READ] = cb_read,
    [FS_CMD_FILE_WRITE] = cb_write,
    [FS_CMD_FILE_SIZE] = cb_fsize,
    [FS_CMD_RENAME] = cb_rename,
    [FS_CMD_FILE_REMOVE] = cb_unlink,
    [FS_CMD_FILE_SYNC] = cb_fsync,
    [FS_CMD_FILE_TRUNCATE] = NULL,
    [FS_CMD_DIR_CREATE] = NULL,
    [FS_CMD_DIR_REMOVE] = NULL,
    [FS_CMD_DIR_OPEN] = NULL,
    [FS_CMD_DIR_CLOSE] = NULL,
    [FS_CMD_DIR_READ] = NULL,
    [FS_CMD_DIR_SEEK] = NULL,
    [FS_CMD_DIR_TELL] = NULL,
    [FS_CMD_DIR_REWIND] = NULL,
};
