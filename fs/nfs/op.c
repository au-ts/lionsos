/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>

#include <fs/protocol.h>

#include "nfs.h"
#include "util.h"
#include "fd.h"

#define MAX_CONCURRENT_OPS 100
#define CLIENT_SHARE_SIZE 0x4000000

struct sddf_fs_queue *command_queue;
struct sddf_fs_queue *completion_queue;
void *client_share;

struct continuation {
    uint64_t request_id;
    uint64_t data[4];
    struct continuation *next_free;
};

struct continuation continuation_pool[MAX_CONCURRENT_OPS];
struct continuation *first_free_cont;

void continuation_pool_init(void) {
    first_free_cont = &continuation_pool[0];
    for (int i = 0; i + 1 < MAX_CONCURRENT_OPS; i++) {
        continuation_pool[i].next_free = &continuation_pool[i+1];
    }
}

struct continuation *continuation_alloc(void) {
    struct continuation *cont = first_free_cont;
    if (cont != NULL) {
        first_free_cont = cont->next_free;
        cont->next_free = NULL;
    }
    return cont;
}

void continuation_free(struct continuation *cont) {
    assert(cont >= continuation_pool);
    assert(cont < continuation_pool + MAX_CONCURRENT_OPS);
    assert(cont->next_free == NULL);
    cont->next_free = first_free_cont;
    first_free_cont = cont;
}

void reply(uint64_t request_id, uint64_t status, uint64_t data0, uint64_t data1) {
    union sddf_fs_message message = {
        .completion = {
            .request_id = request_id,
            .status = status,
            .data = {
                [0] = data0,
                [1] = data1,
            }
        }
    };
    sddf_fs_queue_push(completion_queue, message);
    microkit_notify(CLIENT_CHANNEL);
}

void reply_success(uint64_t request_id, uint64_t data0, uint64_t data1) {
    reply(request_id, 0, data0, data1);
}

void reply_err(uint64_t request_id) {
    reply(request_id, 1, 0, 0);
}

bool buffer_valid(const char *buf, uint64_t len) {
    return buf >= client_share && (buf + len) <= (client_share + CLIENT_SHARE_SIZE);
}

static void stat64_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    void *buf = (void *)cont->data[0];

    if (status == 0) {
        memcpy(buf, data, sizeof (struct nfs_stat_64));
        reply_success(cont->request_id, 0, 0);
    } else {
        dlogp(status != -ENOENT, "failed to stat file (%d): %s", status, data);
        reply_err(cont->request_id);
    }
    continuation_free(cont);
}

void handle_stat64(uint64_t request_id, const char *path, void *buf) {
    int err = 0;

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = (uint64_t)buf;

    err = nfs_stat64_async(nfs, path, stat64_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    reply_err(request_id);
}

void open_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    struct nfsfh *file = data;
    fd_t fd = cont->data[0];

    if (status == 0) {
        fd_set_file(fd, file);
        reply_success(cont->request_id, fd, 0);
    } else {
        dlog("failed to open file (%d): %s\n", status, data);
        fd_free(fd);
        reply_err(cont->request_id);
    }
    continuation_free(cont);
}

void handle_open(uint64_t request_id, const char *path, int flags, int mode) {
    int err;

    fd_t fd;
    err = fd_alloc(&fd);
    if (err) {
        dlog("no free fds");
        goto fail_alloc;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = fd;

    err = nfs_open2_async(nfs, path, flags, mode, open_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    fd_free(fd);
fail_alloc:
    reply_err(request_id);
}

void close_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fd_t fd = cont->data[0];
    struct nfsfh *fh = (struct nfsfh *)cont->data[1];

    if (status == 0) {
        fd_free(fd);
        reply_success(cont->request_id, 0, 0);
    } else {
        dlog("failed to close file: %d (%s)", status, nfs_get_error(nfs));
        fd_set_file(fd, fh);
        reply_err(cont->request_id);
    }
    continuation_free(cont);
}

void handle_close(uint64_t request_id, fd_t fd) {
    int err = 0;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", fd);
        goto fail_begin;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        dlog("fd has outstanding operations\n");
        goto fail_unset;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        err = 1;
        dlog("no free continuations");
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = fd;
    cont->data[1] = (uint64_t)file_handle;

    err = nfs_close_async(nfs, file_handle, close_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    fd_set_file(fd, file_handle);
fail_unset:
fail_begin:
    reply_err(request_id);
}

void pread_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fd_t fd = cont->data[0];

    if (status >= 0) {
        int len_read = status;
        reply_success(cont->request_id, len_read, 0);
    } else {
        dlog("failed to read file: %d (%s)", status, data);
        reply_err(cont->request_id);
    }

    fd_end_op(fd);
    continuation_free(cont);
}

void handle_pread(uint64_t request_id, fd_t fd, const char *buf, uint64_t nbyte, uint64_t offset) {
    int err;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", fd);
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = fd;
    cont->data[1] = (uint64_t)buf;

    err = nfs_pread_async(nfs, file_handle, buf, nbyte, offset, pread_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    fd_end_op(fd);
fail_begin:
    reply_err(request_id);
}

void pwrite_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fd_t fd = cont->data[0];

    if (status >= 0) {
        reply_success(cont->request_id, status, 0);
    } else {
        dlog("failed to write to file: %d (%s)", status, data);
        reply_err(cont->request_id);
    }
    
    fd_end_op(fd);
    continuation_free(cont);
}

void handle_pwrite(uint64_t request_id, fd_t fd, char *buf, uint64_t nbyte, uint64_t offset) {
    int err;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", fd);
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = fd;

    err = nfs_pwrite_async(nfs, file_handle, offset, nbyte, buf, pwrite_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    fd_end_op(fd);
fail_begin:
    reply_err(request_id);
}

void rename_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    if (status == 0) {
        reply_success(cont->request_id, 0, 0);
    } else {
        dlog("failed to write to file: %d (%s)", status, data);
        reply_err(cont->request_id);
    }
    continuation_free(cont);
}

void handle_rename(uint64_t request_id, const char *oldpath, const char *newpath) {
    int err = 0;

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;
    err = nfs_rename_async(nfs, oldpath, newpath, rename_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    reply_err(request_id);
}

void unlink_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    if (status == 0) {
        reply_success(cont->request_id, 0, 0);
    } else {
        reply_err(cont->request_id);
        dlog("failed to unlink file");
    }
    continuation_free(cont);
}

void handle_unlink(uint64_t request_id, const char *path) {
    int err = 0;

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;
    err = nfs_unlink_async(nfs, path, unlink_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    reply_err(request_id);
}

void fsync_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    if (status == 0) {
        reply_success(cont->request_id, 0, 0);
    } else {
        dlog("fsync failed: %d (%s)", status, data);
        reply_err(cont->request_id);
    }
    continuation_free(cont);
}

void handle_fsync(uint64_t request_id, fd_t fd) {
    int err = 0;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd");
        err = 1;
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;

    err = nfs_fsync_async(nfs, file_handle, fsync_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    fd_end_op(fd);
fail_begin:
    reply_err(request_id);
}

void mkdir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    if (status == 0) {
        reply_success(cont->request_id, 0, 0);
    } else {
        dlog("failed to write to file: %d (%s)", status, data);
        reply_err(cont->request_id);
    }
    continuation_free(cont);
}

void handle_mkdir(uint64_t request_id, const char *path) {
    int err;

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;

    err = nfs_mkdir_async(nfs, path, mkdir_cb, NULL);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    reply_err(request_id);
}

void rmdir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = continuation_alloc();
    if (status == 0) {
        reply_success(cont->request_id, 0, 0);
    } else {
        reply_err(cont->request_id);
        dlog("failed to write to file: %d (%s)", status, data);
    }
    continuation_free(cont);
}

void handle_rmdir(uint64_t request_id, const char *path) {
    int err = 0;

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;

    err = nfs_rmdir_async(nfs, path, rmdir_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    reply_err(request_id);
}

void opendir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    
    fd_t fd = cont->data[0];
    struct nfsdir *dir = data;

    if (status == 0) {
        fd_set_dir(fd, dir);
        reply_success(cont->request_id, fd, 0);
    } else {
        dlog("failed to open directory: %d (%s)", status, data);
        reply_err(cont->request_id);
        fd_free(fd);
    }

    continuation_free(cont);
}

void handle_opendir(uint64_t request_id, const char *path) {
    int err = 0;

    fd_t fd;
    err = fd_alloc(&fd);
    if (err) {
        dlog("no free fds");
        goto fail_alloc;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        err = 1;
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = fd;

    err = nfs_opendir_async(nfs, path, opendir_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    fd_free(fd);
fail_alloc:
    reply_err(request_id);
}

void handle_closedir(uint64_t request_id, fd_t fd) {
    int err = 0;

    struct nfsdir *dir_handle = NULL;
    err = fd_begin_op_dir(fd, &dir_handle);
    if (err) {
        dlog("invalid fd");
        goto fail;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        dlog("trying to close fd with outstanding operations");
        goto fail;
    }

    nfs_closedir(nfs, dir_handle);
    fd_free(fd);
fail:
    if (!err) {
        reply_success(request_id, 0, 0);
    } else {
        reply_err(request_id);
    }
}

void handle_readdir(uint64_t request_id, fd_t fd, char *buf, uint64_t buf_size) {
    struct nfsdir *dir_handle = NULL;
    int status = fd_begin_op_dir(fd, &dir_handle);
    if (status) {
        dlog("(%d) invalid fd", status);
        goto fail_begin;
    }

    struct nfsdirent *dirent = nfs_readdir(nfs, dir_handle);
    if (dirent == NULL) {
        status = -1;
        goto fail_readdir;
    }

    uint64_t name_len = strlen(dirent->name) + 1;
    if (name_len > buf_size) {
        dlog("buffer not large enough");
        status = -1;
        goto fail_strcpy;
    }
    strcpy(buf, dirent->name);

fail_strcpy:
fail_readdir:
    fd_end_op(fd);
fail_begin:
    if (status == 0) {
        reply_success(request_id, 0, 0);
    } else {
        reply_err(request_id);
    }
}

void handle_seekdir(uint64_t request_id, fd_t fd, int64_t loc) {
    int err = 0;

    struct nfsdir *dir_handle = NULL;
    err = fd_begin_op_dir(fd, &dir_handle);
    if (err) {
        dlog("invalid fd");
        goto fail;
    }
    nfs_seekdir(nfs, dir_handle, loc);
    fd_end_op(fd);

fail:
    if (!err) {
        reply_success(request_id, 0, 0);
    } else {
        reply_err(request_id);
    }
}

void handle_telldir(uint64_t request_id, fd_t fd) {
    int err = 0;

    struct nfsdir *dir_handle = NULL;
    err = fd_begin_op_dir(fd, &dir_handle);
    if (err) {
        dlog("invalid fd");
        goto fail;
    }
    int64_t loc = nfs_telldir(nfs, dir_handle);
    fd_end_op(fd);

fail:
    if (!err) {
        reply_success(request_id, loc, 0);
    } else {
        reply_err(request_id);
    }
}

void handle_rewinddir(uint64_t request_id, fd_t fd) {
    int err = 0;

    struct nfsdir *dir_handle = NULL;
    err = fd_begin_op_dir(fd, &dir_handle);
    if (err) {
        dlog("invalid fd");
        goto fail;
    }
    nfs_rewinddir(nfs, dir_handle);
    fd_end_op(fd);

fail:
    if (!err) {
        reply_success(request_id, 0, 0);
    } else {
        reply_err(request_id);
    }
}

void nfs_notified(void) {
    union sddf_fs_message message;
    bool success = sddf_fs_queue_pop(command_queue, &message);
    assert(success);

    struct sddf_fs_command cmd = message.command;
    uint64_t request_id = cmd.request_id;
    switch (cmd.cmd_type) {
    case SDDF_FS_CMD_OPEN: {
        uint64_t path_offset = cmd.args[0];
        char *path = client_share + path_offset;
        uint64_t path_len = cmd.args[1];
        if (!buffer_valid(path, path_len)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        path[path_len - 1] = '\0';
        handle_open(request_id, path, O_RDWR | O_CREAT, O_RDWR);
        break;
    }
    case SDDF_FS_CMD_STAT: {
        uint64_t path_offset = cmd.args[0];
        char *path = client_share + path_offset;
        uint64_t path_len = cmd.args[1];
        if (!buffer_valid(path, path_len)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        path[path_len - 1] = '\0';
        uint64_t buf_offset = cmd.args[2];
        char *buf = client_share + buf_offset;
        if (!buffer_valid(buf, sizeof (struct sddf_fs_stat_64))) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        handle_stat64(request_id, path, buf);
        break;
    }
    case SDDF_FS_CMD_CLOSE: {
        fd_t fd = cmd.args[0];
        handle_close(request_id, fd);
        break;
    }
    case SDDF_FS_CMD_PREAD: {
        fd_t fd = cmd.args[0];
        uint64_t buf_offset = cmd.args[1];
        const char *buf = client_share + buf_offset;
        uint64_t nbyte = cmd.args[2];
        uint64_t offset = cmd.args[3];
        if (!buffer_valid(buf, nbyte)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        handle_pread(request_id, fd, buf, nbyte, offset);
        break;
    }
    case SDDF_FS_CMD_PWRITE: {
        fd_t fd = cmd.args[0];
        uint64_t buf_offset = cmd.args[1];
        char *buf = client_share + buf_offset;
        uint64_t nbyte = cmd.args[2];
        uint64_t offset = cmd.args[3];
        if (!buffer_valid(buf, nbyte)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        handle_pwrite(request_id, fd, buf, nbyte, offset);
        break;
    }
    case SDDF_FS_CMD_RENAME: {
        uint64_t oldpath_offset = cmd.args[0];
        uint64_t oldpath_len = cmd.args[1];
        char *oldpath = client_share + oldpath_offset;
        uint64_t newpath_offset = cmd.args[2];
        uint64_t newpath_len = cmd.args[3];
        char *newpath = client_share + newpath_offset;
        if (!buffer_valid(oldpath, oldpath_len) || !buffer_valid(newpath, newpath_len)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        oldpath[oldpath_len - 1] = '\0';
        newpath[newpath_len - 1] = '\0';

        handle_rename(request_id, oldpath, newpath);
        break;
    }
    case SDDF_FS_CMD_UNLINK: {
        uint64_t path_offset = cmd.args[0];
        uint64_t path_len = cmd.args[1];
        char *path = client_share + path_offset;
        if (!buffer_valid(path, path_len)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        path[path_len - 1] = '\0';

        handle_unlink(request_id, path);
        break;
    }
    case SDDF_FS_CMD_MKDIR: {
        uint64_t path_offset = cmd.args[0];
        uint64_t path_len = cmd.args[1];
        char *path = client_share + path_offset;
        if (!buffer_valid(path, path_len)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        path[path_len - 1] = '\0';

        handle_mkdir(request_id, path);
        break;
    }
    case SDDF_FS_CMD_RMDIR: {
        uint64_t path_offset = cmd.args[0];
        uint64_t path_len = cmd.args[1];
        char *path = client_share + path_offset;
        if (!buffer_valid(path, path_len)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        path[path_len - 1] = '\0';

        handle_rmdir(request_id, path);
        break;
    }
    case SDDF_FS_CMD_OPENDIR: {
        uint64_t path_offset = cmd.args[0];
        uint64_t path_len = cmd.args[1];
        char *path = client_share + path_offset;
        if (!buffer_valid(path, path_len)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        path[path_len - 1] = '\0';

        handle_opendir(request_id, path);
        break;
    }
    case SDDF_FS_CMD_CLOSEDIR: {
        fd_t fd = cmd.args[0];
        handle_closedir(request_id, fd);
        break;
    }
    case SDDF_FS_CMD_READDIR: {
        fd_t fd = cmd.args[0];
        uint64_t buf_offset = cmd.args[1];
        uint64_t buf_size = cmd.args[2];
        char *buf = client_share + buf_offset;
        if (!buffer_valid(buf, buf_size)) {
            dlog("bad buffer provided");
            reply_err(request_id);
            break;
        }
        handle_readdir(request_id, fd, buf, buf_size);
        break;
    }
    case SDDF_FS_CMD_FSYNC: {
        fd_t fd = cmd.args[0];
        handle_fsync(request_id, fd);
        break;
    }
    case SDDF_FS_CMD_SEEKDIR: {
        fd_t fd = cmd.args[0];
        int64_t loc = cmd.args[1];
        handle_seekdir(request_id, fd, loc);
        break;
    }
    case SDDF_FS_CMD_TELLDIR: {
        fd_t fd = cmd.args[0];
        handle_telldir(request_id, fd);
        break;
    }
    case SDDF_FS_CMD_REWINDDIR: {
        fd_t fd = cmd.args[0];
        handle_rewinddir(request_id, fd);
        break;
    }
    default:
        dlog("unknown fs operation");
        break;
    }
}
