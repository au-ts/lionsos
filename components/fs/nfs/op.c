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

#include <lions/fs/protocol.h>

#include "nfs.h"
#include "util.h"
#include "fd.h"

#define MAX_CONCURRENT_OPS FS_QUEUE_CAPACITY
#define CLIENT_SHARE_SIZE 0x4000000

struct fs_queue *command_queue;
struct fs_queue *completion_queue;
char *client_share;

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
    fs_msg_t message = {
        .cmpl = {
            .id = request_id,
            .status = status,
            .data = {
                [0] = data0,
                [1] = data1,
            }
        }
    };
    fs_queue_push(completion_queue, message);
    microkit_notify(CLIENT_CHANNEL);
}

void reply_success(uint64_t request_id, uint64_t data0, uint64_t data1) {
    reply(request_id, 0, data0, data1);
}

void reply_err(uint64_t request_id) {
    reply(request_id, 1, 0, 0);
}

void *get_buffer(fs_buffer_t buf) {
    if (buf.offset + buf.size > CLIENT_SHARE_SIZE) {
        return NULL;
    }
    return (void *)(client_share + buf.offset);
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
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_continuation:
    reply_err(request_id);
}

void fstat_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fd_t fd = cont->data[0];
    void *buf = (void *)cont->data[1];

    if (status == 0) {
        memcpy(buf, data, sizeof (struct nfs_stat_64));
        reply_success(cont->request_id, 0, 0);
    } else {
        dlog("failed to stat file (fd=%lu) (%d): %s", fd, status, data);
        reply_err(cont->request_id);
    }
    fd_end_op(fd);
    continuation_free(cont);
}

void handle_fstat(uint64_t request_id, fd_t fd, void *buf) {
    int err = 0;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", fd);
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = fd;
    cont->data[1] = (uint64_t) buf;

    err = nfs_fstat64_async(nfs, file_handle, fstat_cb, cont);
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

void handle_open(uint64_t request_id, const char *path, uint64_t flags) {
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

    int posix_flags = 0;
    if (flags & FS_OPEN_FLAGS_READ_ONLY) {
        posix_flags |= O_RDONLY;
    }
    if (flags & FS_OPEN_FLAGS_WRITE_ONLY) {
        posix_flags |= O_WRONLY;
    }
    if (flags & FS_OPEN_FLAGS_READ_WRITE) {
        posix_flags |= O_RDWR;
    }
    if (flags & FS_OPEN_FLAGS_CREATE) {
        posix_flags |= O_CREAT;
    }

    err = nfs_open2_async(nfs, path, posix_flags, 0644, open_cb, cont);
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

void handle_pread(uint64_t request_id, fd_t fd, char *buf, uint64_t nbyte, uint64_t offset) {
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

void handle_pwrite(uint64_t request_id, fd_t fd, const char *buf, uint64_t nbyte, uint64_t offset) {
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

    err = nfs_pwrite_async(nfs, file_handle, buf, nbyte, offset, pwrite_cb, cont);
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
    fd_t fd = cont->data[0];
    if (status == 0) {
        reply_success(cont->request_id, 0, 0);
    } else {
        dlog("fsync failed: %d (%s)", status, data);
        reply_err(cont->request_id);
    }
    fd_end_op(fd);
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
    cont->data[0] = fd;

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

void truncate_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fd_t fd = cont->data[0];
    if (status == 0) {
        reply_success(cont->request_id, 0, 0);
    } else {
        dlog("ftruncate failed: %d (%s)", status, data);
        reply_err(cont->request_id);
    }
    fd_end_op(fd);
    continuation_free(cont);
}

void handle_truncate(uint64_t request_id, fd_t fd, uint64_t length) {
    int err = 0;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd");
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    if (cont == NULL) {
        dlog("no free continuations");
        goto fail_continuation;
    }
    cont->request_id = request_id;
    cont->data[0] = fd;

    err = nfs_ftruncate_async(nfs, file_handle, length, truncate_cb, cont);
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

    err = nfs_mkdir_async(nfs, path, mkdir_cb, cont);
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
    fs_msg_t message;
    while (fs_queue_pop(command_queue, &message)) {
        fs_cmd_t cmd = message.cmd;
        uint64_t request_id = cmd.id;
        switch (cmd.type) {
        case FS_CMD_OPEN: {
            struct fs_cmd_params_open *params = &cmd.params.open;
            char *path = get_buffer(params->path);
            if (path == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            path[params->path.size - 1] = '\0';
            handle_open(request_id, path, params->flags);
            break;
        }
        case FS_CMD_STAT: {
            struct fs_cmd_params_stat *params = &cmd.params.stat;
            char *path = get_buffer(params->path);
            if (path == NULL) {
                dlog("bad path buffer provided");
                reply_err(request_id);
                break;
            }
            path[params->path.size - 1] = '\0';
            void *buf = get_buffer(params->buf);
            if (buf == NULL) {
                dlog("bad stat buffer provided");
                reply_err(request_id);
                break;
            }
            handle_stat64(request_id, path, buf);
            break;
        }
        case FS_CMD_FSTAT: {
            struct fs_cmd_params_fstat *params = &cmd.params.fstat;
            void *buf = get_buffer(params->buf);
            if (buf == NULL || params->buf.size < sizeof (fs_stat_t)) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            handle_fstat(request_id, params->fd, buf);
            break;
        }
        case FS_CMD_CLOSE: {
            struct fs_cmd_params_close *params = &cmd.params.close;
            handle_close(request_id, params->fd);
            break;
        }
        case FS_CMD_READ: {
            struct fs_cmd_params_read *params = &cmd.params.read;
            const char *buf = get_buffer(params->buf);
            if (buf == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            handle_pread(request_id, params->fd, buf, params->buf.size, params->offset);
            break;
        }
        case FS_CMD_WRITE: {
            struct fs_cmd_params_write *params = &cmd.params.write;
            char *buf = get_buffer(params->buf);
            if (buf == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            handle_pwrite(request_id, params->fd, buf, params->buf.size, params->offset);
            break;
        }
        case FS_CMD_RENAME: {
            struct fs_cmd_params_rename *params = &cmd.params.rename;
            char *old_path = get_buffer(params->old_path);
            char *new_path = get_buffer(params->new_path);
            if (old_path == NULL || new_path == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            old_path[params->old_path.size - 1] = '\0';
            new_path[params->new_path.size - 1] = '\0';
            handle_rename(request_id, old_path, new_path);
            break;
        }
        case FS_CMD_UNLINK: {
            struct fs_cmd_params_unlink *params = &cmd.params.unlink;
            char *path = get_buffer(params->path);
            if (path == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            path[params->path.size - 1] = '\0';
            handle_unlink(request_id, path);
            break;
        }
        case FS_CMD_TRUNCATE: {
            struct fs_cmd_params_truncate *params = &cmd.params.truncate;
            handle_truncate(request_id, params->fd, params->length);
            break;
        }
        case FS_CMD_MKDIR: {
            struct fs_cmd_params_mkdir *params = &cmd.params.mkdir;
            char *path = get_buffer(params->path);
            if (path == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            path[params->path.size - 1] = '\0';
            handle_mkdir(request_id, path);
            break;
        }
        case FS_CMD_RMDIR: {
            struct fs_cmd_params_rmdir *params = &cmd.params.rmdir;
            char *path = get_buffer(params->path);
            if (path == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            path[params->path.size - 1] = '\0';
            handle_rmdir(request_id, path);
            break;
        }
        case FS_CMD_OPENDIR: {
            struct fs_cmd_params_opendir *params = &cmd.params.opendir;
            char *path = get_buffer(params->path);
            if (path == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            path[params->path.size - 1] = '\0';
            handle_opendir(request_id, path);
            break;
        }
        case FS_CMD_CLOSEDIR: {
            struct fs_cmd_params_closedir *params = &cmd.params.closedir;
            handle_closedir(request_id, params->fd);
            break;
        }
        case FS_CMD_READDIR: {
            struct fs_cmd_params_readdir *params = &cmd.params.readdir;
            char *buf = get_buffer(params->buf);
            if (buf == NULL) {
                dlog("bad buffer provided");
                reply_err(request_id);
                break;
            }
            handle_readdir(request_id, params->fd, buf, params->buf.size);
            break;
        }
        case FS_CMD_FSYNC: {
            struct fs_cmd_params_fsync *params = &cmd.params.fsync;
            handle_fsync(request_id, params->fd);
            break;
        }
        case FS_CMD_SEEKDIR: {
            struct fs_cmd_params_seekdir *params = &cmd.params.seekdir;
            handle_seekdir(request_id, params->fd, params->loc);
            break;
        }
        case FS_CMD_TELLDIR: {
            struct fs_cmd_params_telldir *params = &cmd.params.telldir;
            handle_telldir(request_id, params->fd);
            break;
        }
        case FS_CMD_REWINDDIR: {
            struct fs_cmd_params_rewinddir *params = &cmd.params.rewinddir;
            handle_rewinddir(request_id, params->fd);
            break;
        }
        default:
            dlog("unknown fs operation");
            break;
        }
    }
}
