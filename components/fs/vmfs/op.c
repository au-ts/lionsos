/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mount.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/syscall.h>

#include <lions/fs/protocol.h>

#include "vmfs_shared.h"
#include "log.h"
#include "op.h"
#include "util.h"

#include <liburing.h>

#define NAME_MAX_LEN 256

/* From main.c */
extern char blk_device[PATH_MAX];
extern int blk_device_len;
extern char mnt_point[PATH_MAX];
extern int mnt_point_len;
extern char *fs_data;
extern struct io_uring ring;

/* Metadata with the mounted FS */
bool mounted = false;
extern char *our_data_region;

#define TIMESPEC_TO_NS(ts) ((long long)ts.tv_sec * 1000000000LL + ts.tv_nsec)

#define CREATE_COMP(cmd_id, status_code, return_data) \
    (fs_cmpl_t){ .id = (cmd_id), .status = (status_code), .data = (return_data) }

#define CHECK_MOUNTED(cmd_id, comp_idx) \
    do { \
        if (!mounted) { \
            fs_queue_enqueue_reply(CREATE_COMP((cmd_id), FS_STATUS_ERROR, (fs_cmpl_data_t){0}), (comp_idx)); \
            return; \
        } \
    } while (0)

#define SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx) \
    do { \
        /* Ensure memory for the callback structure. */ \
        cb_data = malloc(sizeof(io_uring_comp_callback_t)); \
        if (!cb_data) { \
            LOG_FS_ERR("handle_open(): out of memory for callback structure\n"); \
            fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(ENOMEM), (fs_cmpl_data_t){0}), comp_idx); \
            exit(EXIT_FAILURE); \
        } \
        /* Get a submission queue entry (sqe).
        This should never fail because the io_uring queue is the same capacity as client queue. */ \
        sqe = io_uring_get_sqe(&ring); \
        if (!sqe) { \
            LOG_FS_ERR("handle_open(): io_uring_get_sqe(): cannot get an SQE\n"); \
            fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_ERROR, (fs_cmpl_data_t){0}), comp_idx); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/* Thin error checking wrapper around fs_malloc_create_path. Prepend the
   the mount point with the client path */
char *malloc_prepare_path(fs_buffer_t their_path, uint64_t cmd_id, uint64_t *comp_idx) {
    size_t expected_path_total_len = mnt_point_len + their_path.size;
    if (expected_path_total_len > PATH_MAX) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd_id, errno_to_lions_status(ENAMETOOLONG), (fs_cmpl_data_t){0}), comp_idx);
    }
    size_t got_path_total_len;
    char *path = fs_malloc_create_path(their_path, &got_path_total_len);
    if (!path) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd_id, errno_to_lions_status(ENOMEM), (fs_cmpl_data_t){0}), comp_idx);
        LOG_FS_ERR("malloc_prepare_path(): ENOMEM, bail!");
        exit(EXIT_FAILURE);
    }
    assert(expected_path_total_len == got_path_total_len);
    return path;
}

uint64_t concat_2_32_bits(uint32_t lhs, uint32_t rhs) {
    uint64_t res = rhs;
    res = res >> 32;
    res |= lhs;
    return res;
}

/* Mounting */
void handle_initialise(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_initialise(): entry\n");

    if (mounted) {
        LOG_FS_OPS("handle_initialise(): already mounted!\n");
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_ERROR, (fs_cmpl_data_t){0}), comp_idx);
    }

    /* Use the shell to mount the filesystem */
    uint64_t cmd_size = sizeof(char) * (16 + blk_device_len + mnt_point_len);
    char *sh_mount_cmd = malloc(cmd_size);
    if (!sh_mount_cmd) {
        LOG_FS_ERR("handle_initialise(): out of memory, bail.\n");
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(ENOMEM), (fs_cmpl_data_t){0}), comp_idx);
        exit(EXIT_FAILURE);
    }

    /* Mount without caching! */
    snprintf(sh_mount_cmd, cmd_size, "mount -o sync %s %s", blk_device, mnt_point);
    LOG_FS_OPS("handle_initialise(): mounting with shell command: %s\n", sh_mount_cmd);
    int ret = system(sh_mount_cmd);
    free(sh_mount_cmd);

    if (ret == 0) {
        mounted = true;
        LOG_FS_OPS("handle_initialise(): block device at %s mounted at %s\n", blk_device, mnt_point);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        LOG_FS_OPS("handle_initialise(): failed to mount block device at %s\n", blk_device);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_ERROR, (fs_cmpl_data_t){0}), comp_idx);
    }
}

/* Unmounting */
void handle_deinitialise(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_deinitialise(): entry\n");

    CHECK_MOUNTED(cmd.id, comp_idx);

    /* Use the shell to unmount the filesystem */
    uint64_t cmd_size = sizeof(char) * ((PATH_MAX) + 16);
    char *sh_umount_cmd = malloc(cmd_size);
    if (!sh_umount_cmd) {
        LOG_FS_ERR("handle_deinitialise(): out of memory, bail.\n");
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(ENOMEM), (fs_cmpl_data_t){0}), comp_idx);
        exit(EXIT_FAILURE);
    }

    snprintf(sh_umount_cmd, cmd_size, "umount %s", mnt_point);
    LOG_FS_OPS("handle_deinitialise(): unmounting with shell command: %s\n", sh_umount_cmd);
    int umount_exit_code = system(sh_umount_cmd);
    free(sh_umount_cmd);

    if (umount_exit_code == 0) {
        mounted = false;
        LOG_FS_OPS("handle_deinitialise(): filesystem at %s, with backing block device at %s UNMOUNTED.\n", mnt_point, blk_device);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_ERROR, (fs_cmpl_data_t){0}), comp_idx);
    }
}

/* Open */
void handle_open(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_open(): entry\n");

    struct fs_cmd_params_file_open params = cmd.params.file_open;
    char *path = malloc_prepare_path(params.path, cmd.id, comp_idx);
    if (!path) {
        return;
    }

    LOG_FS_OPS("handle_open(): got path %s\n", path);
    int flags = 0;
    if (params.flags & FS_OPEN_FLAGS_CREATE) {
        flags |= O_CREAT;
    }

    if (params.flags & FS_OPEN_FLAGS_READ_WRITE) {
        flags |= O_RDWR;
    } else if (params.flags & FS_OPEN_FLAGS_READ_ONLY) {
        flags |= O_RDONLY;
    } else if (params.flags & FS_OPEN_FLAGS_WRITE_ONLY) {
        flags |= O_WRONLY;
    }

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    /* Now fill in and enqueue an open operation. */
    io_uring_prep_open(sqe, path, flags, 0);

    /* Prepare the callback data structure */
    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_FILE_OPEN;
    cb_data->malloced_data_1 = path;
    cb_data->malloced_data_2 = (char *) NULL;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_open(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    int fd = cqe->res;

    if (fd >= 0) {
        fs_cmpl_data_t result;
        result.file_open.fd = fd;
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, result), comp_idx);
        LOG_FS_OPS("cb_open(): success\n");
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-fd), (fs_cmpl_data_t){0}), comp_idx);
        LOG_FS_OPS("cb_open(): fail: %d %s\n", -cqe->res, strerror(-cqe->res));
    }

    assert(cb_data->malloced_data_1);
    free(cb_data->malloced_data_1);
    assert(!cb_data->malloced_data_2);
    free(cb_data);
}

/* Stat */
void handle_stat(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_stat(): entry\n");
    fs_cmd_params_stat_t params = cmd.params.stat;

    if (params.buf.size < sizeof(fs_stat_t)) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(EFAULT), (fs_cmpl_data_t){0}), comp_idx);
        return;
    }

    char *path = malloc_prepare_path(params.path, cmd.id, comp_idx);
    if (!path) {
        return;
    }

    struct statx *stx = malloc(sizeof(struct statx));
    if (!stx) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(ENAMETOOLONG), (fs_cmpl_data_t){0}), comp_idx);
        return;
    }

    LOG_FS_OPS("handle_stat(): got concatenated path %s\n", path);

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    /* Now fill in and enqueue a statx operation. */
    io_uring_prep_statx(sqe, AT_FDCWD, path, 0, STATX_BASIC_STATS, stx);

    /* Prepare the callback data structure */
    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_STAT;
    cb_data->resp_buf = params.buf;
    cb_data->malloced_data_1 = path;
    cb_data->malloced_data_2 = (char *) stx;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_stat(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    assert(cb_data->malloced_data_1);
    assert(cb_data->malloced_data_2);

    if (cqe->res == 0) {
        struct statx *stat_data = (struct statx *) cb_data->malloced_data_2;
        fs_stat_t *resp_stat = (fs_stat_t *) fs_get_buffer(cb_data->resp_buf);

        resp_stat->dev = concat_2_32_bits(stat_data->stx_dev_major, stat_data->stx_dev_minor);
        resp_stat->ino = stat_data->stx_ino;
        resp_stat->mode = stat_data->stx_mode;
        resp_stat->nlink = stat_data->stx_nlink;
        resp_stat->uid = stat_data->stx_uid;
        resp_stat->gid = stat_data->stx_gid;
        resp_stat->rdev = concat_2_32_bits(stat_data->stx_rdev_major, stat_data->stx_rdev_minor);
        resp_stat->size = stat_data->stx_size;
        resp_stat->blksize = stat_data->stx_blksize;
        resp_stat->blocks = stat_data->stx_blocks;

        resp_stat->atime = stat_data->stx_atime.tv_sec;
        resp_stat->mtime = stat_data->stx_mtime.tv_sec;
        resp_stat->ctime = stat_data->stx_ctime.tv_sec;

        resp_stat->atime_nsec = TIMESPEC_TO_NS(stat_data->stx_atime);
        resp_stat->mtime_nsec = TIMESPEC_TO_NS(stat_data->stx_mtime);
        resp_stat->ctime_nsec = TIMESPEC_TO_NS(stat_data->stx_ctime);
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
        LOG_FS_OPS("cb_stat(): success\n");
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-cqe->res), (fs_cmpl_data_t){0}), comp_idx);
        LOG_FS_OPS("cb_stat(): fail: %d %s\n", -cqe->res, strerror(-cqe->res));
    }

    free(cb_data->malloced_data_1);
    free(cb_data->malloced_data_2);
    free(cb_data);
}

/* Fsize */

void handle_fsize(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_fsize(): entry\n");
    fs_cmd_params_file_size_t params = cmd.params.file_size;
    uint64_t fd = params.fd;

    struct statx *stx = malloc(sizeof(struct statx));
    if (!stx) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(ENAMETOOLONG), (fs_cmpl_data_t){0}), comp_idx);
        return;
    }

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    io_uring_prep_statx(sqe, fd, "", AT_EMPTY_PATH, STATX_SIZE, stx);

    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_FILE_SIZE;
    cb_data->malloced_data_1 = (char *) stx;
    cb_data->malloced_data_2 = (char *) NULL;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_fsize(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    assert(cb_data->malloced_data_1);

    if (cqe->res == 0) {
        struct statx *stat_data = (struct statx *) cb_data->malloced_data_1;
        fs_cmpl_data_t result;
        result.file_size.size = stat_data->stx_size;
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, result), comp_idx);
        LOG_FS_OPS("cb_fsize(): success\n");
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-cqe->res), (fs_cmpl_data_t){0}), comp_idx);
        LOG_FS_OPS("cb_fsize(): fail: %d %s\n", -cqe->res, strerror(-cqe->res));
    }

    free(cb_data->malloced_data_1);
    assert(!cb_data->malloced_data_2);
    free(cb_data);
}

/* Close */

void handle_close(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_close(): entry\n");

    fs_cmd_params_file_close_t params = cmd.params.file_close;
    uint64_t fd = params.fd;

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    io_uring_prep_close(sqe, fd);

    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_FILE_CLOSE;
    cb_data->malloced_data_1 = (char *) NULL;
    cb_data->malloced_data_2 = (char *) NULL;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_close(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    int err = cqe->res;

    if (err == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-err), (fs_cmpl_data_t){0}), comp_idx);
    }

    assert(!cb_data->malloced_data_1);
    assert(!cb_data->malloced_data_2);
    free(cb_data);
}

/* Read */

void handle_read(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_read(): entry\n");
    fs_cmd_params_file_read_t params = cmd.params.file_read;

    uint64_t fd = params.fd;
    uint64_t count = params.buf.size;
    uint64_t off = params.offset;

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    io_uring_prep_read(sqe, fd, (char *) ((uint64_t) our_data_region + params.buf.offset), count, off);

    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_FILE_READ;
    /* This isn't malloced! Bit hacky */
    cb_data->resp_buf = params.buf;
    cb_data->malloced_data_1 = (char *) ((uint64_t) our_data_region + params.buf.offset);
    cb_data->malloced_data_2 = (char *) NULL;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_read(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    int nbytes = cqe->res;
    assert(cb_data->malloced_data_1);

    if (nbytes >= 0) {
        fs_memcpy(fs_get_buffer(cb_data->resp_buf), cb_data->malloced_data_1, nbytes);
        fs_cmpl_data_t result;
        result.file_read.len_read = nbytes;
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, result), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-nbytes), (fs_cmpl_data_t){0}), comp_idx);
    }

    assert(!cb_data->malloced_data_2);
    free(cb_data);
}

/* Write */

void handle_write(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_write(): entry\n");
    fs_cmd_params_file_write_t params = cmd.params.file_write;

    uint64_t fd = params.fd;
    uint64_t count = params.buf.size;
    uint64_t off = params.offset;

    LOG_FS_OPS("count = %lu, off = %lu, buff = %p\n", count, off, fs_get_buffer(params.buf));

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    /* Copy the client's data into our buffer due to UIO being treated as device memory... */
    fs_memcpy((char *) ((uint64_t) our_data_region + params.buf.offset), fs_get_buffer(params.buf), count);

    io_uring_prep_write(sqe, fd, (char *) ((uint64_t) our_data_region + params.buf.offset), count, off);

    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_FILE_WRITE;
    /* This isn't malloced! Bit hacky */
    cb_data->malloced_data_1 = (char *) ((uint64_t) our_data_region + params.buf.offset);
    cb_data->malloced_data_2 = (char *) NULL;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_write(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    int nbytes = cqe->res;
    assert(cb_data->malloced_data_1);

    if (nbytes >= 0) {
        fs_cmpl_data_t result;
        result.file_write.len_written = nbytes;
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, result), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-nbytes), (fs_cmpl_data_t){0}), comp_idx);
    }

    assert(!cb_data->malloced_data_2);
    free(cb_data);
}

/* Rename */

void handle_rename(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_rename(): entry\n");
    fs_cmd_params_rename_t params = cmd.params.rename;

    char *src_path = malloc_prepare_path(params.old_path, cmd.id, comp_idx);
    if (!src_path) {
        return;
    }
    char *dst_path = malloc_prepare_path(params.new_path, cmd.id, comp_idx);
    if (!dst_path) {
        free(src_path);
        return;
    }

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    io_uring_prep_rename(sqe, src_path, dst_path);

    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_RENAME;
    cb_data->malloced_data_1 = src_path;
    cb_data->malloced_data_2 = dst_path;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_rename(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    int err = cqe->res;

    if (err == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-err), (fs_cmpl_data_t){0}), comp_idx);
    }

    assert(cb_data->malloced_data_1);
    free(cb_data->malloced_data_1);
    assert(cb_data->malloced_data_2);
    free(cb_data->malloced_data_2);
    free(cb_data);
}

/* Unlink */

void handle_unlink(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_unlink(): entry\n");
    fs_cmd_params_file_remove_t params = cmd.params.file_remove;
    char *path = malloc_prepare_path(params.path, cmd.id, comp_idx);
    if (!path) {
        return;
    }

    LOG_FS_OPS("handle_unlink(): got concatenated path %s\n", path);

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    io_uring_prep_unlink(sqe, path, 0);

    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_FILE_REMOVE;
    cb_data->malloced_data_1 = path;
    cb_data->malloced_data_2 = (char *) NULL;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_unlink(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    int err = cqe->res;

    if (err == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-err), (fs_cmpl_data_t){0}), comp_idx);
    }

    assert(cb_data->malloced_data_1);
    free(cb_data->malloced_data_1);
    assert(!cb_data->malloced_data_2);
    free(cb_data);
}

/* Truncate */

void handle_truncate(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_truncate(): entry\n");

    fs_cmd_params_file_truncate_t params = cmd.params.file_truncate;
    uint64_t fd = params.fd;
    uint64_t len = params.length;

    if (!io_uring_sqe_queue_empty(&ring)) {
        flush_and_wait_io_uring_sqes(&ring, comp_idx);
    }

    if (ftruncate(fd, len) == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        int err_num = errno;
        LOG_FS_OPS("handle_truncate(): fail with errno %d\n", err_num);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(err_num), (fs_cmpl_data_t){0}), comp_idx);
    }
}

/* Fsync */

void handle_fsync(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_fsync(): entry\n");

    fs_cmd_params_file_sync_t params = cmd.params.file_sync;
    uint64_t fd = params.fd;

    io_uring_comp_callback_t *cb_data = malloc(sizeof(io_uring_comp_callback_t));
    struct io_uring_sqe *sqe;
    SET_UP_IO_URING_REQUEST(cb_data, sqe, cmd, comp_idx);

    io_uring_prep_fsync(sqe, fd, 0);

    cb_data->cmd_id = cmd.id;
    cb_data->cmd_type = FS_CMD_FILE_SYNC;
    cb_data->malloced_data_1 = (char *) NULL;
    cb_data->malloced_data_2 = (char *) NULL;
    sqe->user_data = (uint64_t) cb_data;
}

void cb_fsync(struct io_uring_cqe *cqe, uint64_t *comp_idx)
{
    io_uring_comp_callback_t *cb_data = cb_dat_from_cqe(cqe);
    int err = cqe->res;

    if (err == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cb_data->cmd_id, errno_to_lions_status(-err), (fs_cmpl_data_t){0}), comp_idx);
    }

    assert(!cb_data->malloced_data_1);
    assert(!cb_data->malloced_data_2);
    free(cb_data);
}

/* Mkdir */

void handle_mkdir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_mkdir(): entry\n");

    if (!io_uring_sqe_queue_empty(&ring)) {
        flush_and_wait_io_uring_sqes(&ring, comp_idx);
    }

    /* Concatenate the client provided path with the mount point. */
    fs_cmd_params_dir_remove_t params = cmd.params.dir_remove;
    char *path = malloc_prepare_path(params.path, cmd.id, comp_idx);
    if (!path) {
        return;
    }

    LOG_FS_OPS("handle_mkdir(): got concatenated path %s\n", path);

    if (mkdir(path, 0) == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        int err_num = errno;
        LOG_FS_OPS("handle_mkdir(): fail with errno %d\n", err_num);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(err_num), (fs_cmpl_data_t){0}), comp_idx);
    }

    free(path);
}

/* Rmdir */
void handle_rmdir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_rmdir(): entry\n");

    if (!io_uring_sqe_queue_empty(&ring)) {
        flush_and_wait_io_uring_sqes(&ring, comp_idx);
    }

    /* Concatenate the client provided path with the mount point. */
    fs_cmd_params_dir_remove_t params = cmd.params.dir_remove;
    char *path = malloc_prepare_path(params.path, cmd.id, comp_idx);
    if (!path) {
        return;
    }

    LOG_FS_OPS("handle_rmdir(): got concatenated path %s\n", path);

    if (rmdir(path) == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        int err_num = errno;
        LOG_FS_OPS("handle_opendir(): fail with errno %d\n", err_num);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(err_num), (fs_cmpl_data_t){0}), comp_idx);
    }

    free(path);
}

/* Opendir */

void handle_opendir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_opendir(): entry\n");

    /* Concatenate the client provided path with the mount point. */
    fs_cmd_params_dir_open_t params = cmd.params.dir_open;
    char *path = malloc_prepare_path(params.path, cmd.id, comp_idx);
    if (!path) {
        return;
    }

    LOG_FS_OPS("handle_opendir(): got concatenated path %s\n", path);

    /* Using opendir instead of open for better portability. */
    DIR *dir_stream = opendir(path);
    if (dir_stream) {
        LOG_FS_OPS("handle_opendir(): ok\n");
        fs_cmpl_data_t result;
        /* This is very sketchy, setting the "fd" as the pointer to dir_stream. */
        result.dir_open.fd = (uint64_t) dir_stream;
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, result), comp_idx);
    } else {
        int err_num = errno;
        LOG_FS_OPS("handle_opendir(): fail with errno %d\n", err_num);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(err_num), (fs_cmpl_data_t){0}), comp_idx);
    }

    free(path);
}

/* Closedir */

void handle_closedir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_closedir(): entry\n");
    fs_cmd_params_dir_close_t params = cmd.params.dir_close;
    DIR *dir_stream = (DIR *) params.fd;

    if (closedir(dir_stream) == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        int err_num = errno;
        LOG_FS_OPS("handle_closedir(): fail with errno %d\n", err_num);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(err_num), (fs_cmpl_data_t){0}), comp_idx);
    }
}

/* Readdir */

void handle_readdir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_readdir(): entry\n");
    fs_cmd_params_dir_read_t params = cmd.params.dir_read;
    DIR *dir_stream = (DIR *) params.fd;
    char *path = fs_get_buffer(params.buf);

    if (params.buf.size < NAME_MAX_LEN) {
        LOG_FS_OPS("handle_readdir(): client buf not big enough: %lu < %d\n", params.buf.size, NAME_MAX_LEN);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_INVALID_BUFFER, (fs_cmpl_data_t){0}), comp_idx);
        return;
    }

    errno = 0;
    struct dirent *entry = readdir(dir_stream);
    if (!entry) {
        if (errno == 0) {
            fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_END_OF_DIRECTORY, (fs_cmpl_data_t){0}), comp_idx);
            return;
        } else {
            int err_num = errno;
            LOG_FS_OPS("handle_readdir(): fail with errno %d\n", err_num);
            fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(err_num), (fs_cmpl_data_t){0}), comp_idx);
            return;
        }
    }

    size_t name_len = strlen(entry->d_name);
    strcpy(path, entry->d_name);

    fs_cmpl_data_t result;
    result.dir_read.path_len = name_len;
    fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, result), comp_idx);
}

/* Seekdir */

void handle_seekdir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_seekdir(): entry\n");

    fs_cmd_params_dir_seek_t params = cmd.params.dir_seek;
    DIR *dir_stream = (DIR *) params.fd;
    int64_t loc = params.loc;

    errno = 0;
    seekdir(dir_stream, loc);

    if (errno == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(errno), (fs_cmpl_data_t){0}), comp_idx);
    }
}

/* Telldir */

void handle_telldir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_telldir(): entry\n");

    fs_cmd_params_dir_tell_t params = cmd.params.dir_tell;
    DIR *dir_stream = (DIR *) params.fd;

    long pos = telldir(dir_stream);
    if (pos != -1) {
        fs_cmpl_data_t result;
        result.dir_tell.location = pos;
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, result), comp_idx);
    } else {
        int err_num = errno;
        LOG_FS_OPS("handle_telldir(): fail with errno %d\n", err_num);
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(err_num), (fs_cmpl_data_t){0}), comp_idx);
    }
}

/* Rewinddir*/

void handle_rewinddir(fs_cmd_t cmd, uint64_t *comp_idx)
{
    LOG_FS_OPS("handle_rewinddir(): entry\n");

    fs_cmd_params_dir_rewind_t params = cmd.params.dir_rewind;
    DIR *dir_stream = (DIR *) params.fd;

    errno = 0;
    rewinddir(dir_stream);

    if (errno == 0) {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, FS_STATUS_SUCCESS, (fs_cmpl_data_t){0}), comp_idx);
    } else {
        fs_queue_enqueue_reply(CREATE_COMP(cmd.id, errno_to_lions_status(errno), (fs_cmpl_data_t){0}), comp_idx);
    }
}
