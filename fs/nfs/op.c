#include <microkit.h>

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

static void stat64_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    memcpy(client_share, &status, 4);
    if (status == 0) {
        struct nfs_stat_64 *st = (struct nfs_stat_64 *)data;
        memcpy(client_share + 4, st, sizeof *st);
    } else {
        dlog("failed to stat file (%d): %s", status, data);
    }
    microkit_notify(CLIENT_CHANNEL);
}

void handle_stat64(const char *path) {
    int err = nfs_stat64_async(nfs, path, stat64_cb, (void *)CLIENT_CHANNEL);
    if (err) {
        dlog("failed to enqueue command");
        memcpy(client_share, &err, 4);
        microkit_notify(CLIENT_CHANNEL);
    }
}

void open_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct nfsfh *file = data;
    fd_t fd = (fd_t)private_data;

    if (status == 0) {
        fd_set_file(fd, file);
        memcpy(client_share + 4, &fd, 8);
    } else {
        dlog("failed to open file\n");
        fd_free(fd);
    }
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_open(const char *path, int flags, int mode) {
    int err;

    fd_t fd;
    err = fd_alloc(&fd);
    if (err) {
        dlog("no free fds");
        goto fail_alloc;
    }
    err = nfs_open2_async(nfs, path, flags, mode, open_cb, (void *)fd);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }
    return;

fail_enqueue:
    fd_free(fd);
fail_alloc:
    memcpy(client_share, &err, sizeof err);
    microkit_notify(CLIENT_CHANNEL);
}

struct close_data {
    fd_t fd;
    struct nfsfh *file_handle;
};

void close_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    dlogp(status != 0, "failed to close file: %d (%s)", status, nfs_get_error(nfs));

    struct close_data *close_data = private_data;
    if (status == 0) {
        fd_free(close_data->fd);
    } else {
        fd_set_file(close_data->fd, close_data->file_handle);
    }
    free(close_data);

    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_close(fd_t fd) {
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

    struct close_data *close_data = malloc(sizeof *close_data);
    close_data->fd = fd;
    close_data->file_handle = file_handle;
    err = nfs_close_async(nfs, file_handle, close_cb, (void *)close_data);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    fd_set_file(fd, file_handle);
fail_unset:
fail_begin:
    memcpy(client_share, &err, sizeof err);
    microkit_notify(CLIENT_CHANNEL);
}

void pread_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    dlogp(status < 0, "failed to read file: %d (%s)", status, data);

    fd_t fd = (fd_t)private_data;
    if (status >= 0) {
        int len_read = status;
        memcpy(client_share + 4, data, len_read);
    }

    fd_end_op(fd);
    memcpy(client_share, &status, sizeof status);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_pread(fd_t fd, uint64_t nbyte, uint64_t offset) {
    int err;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", fd);
        goto fail_begin;
    }

    err = nfs_pread_async(nfs, file_handle, offset, nbyte, pread_cb, (void *)fd);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    fd_end_op(fd);
fail_begin:
    memcpy(client_share, &err, sizeof err);
    microkit_notify(CLIENT_CHANNEL);
}

void pwrite_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    dlogp(status < 0, "failed to write to file: %d (%s)", status, data);

    fd_t fd = (fd_t)private_data;
    fd_end_op(fd);
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_pwrite(fd_t fd, uint64_t nbyte, uint64_t offset) {
    int err;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", fd);
        goto fail_begin;
    }

    err = nfs_pwrite_async(nfs, file_handle, offset, nbyte, client_share, pwrite_cb, (void *)fd);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    fd_end_op(fd);
fail_begin:
    memcpy(client_share, &err, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void rename_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    dlogp(status != 0, "failed to write to file: %d (%s)", status, data);
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_rename(const char *oldpath, const char *newpath) {
    int err = nfs_rename_async(nfs, oldpath, newpath, rename_cb, NULL);
    if (err) {
        dlog("failed to enqueue command");
        microkit_notify(CLIENT_CHANNEL);
    }
}

void unlink_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    dlogp(status != 0, "failed to write to file: %d (%s)", status, data);
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_unlink(const char *path) {
    int err = nfs_unlink_async(nfs, path, unlink_cb, NULL);
    if (err) {
        dlog("failed to enqueue command");
        microkit_notify(CLIENT_CHANNEL);
    }
}

void fsync_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    dlogp(status != 0, "fsync failed: %d (%s)", status, data);
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_fsync(fd_t fd) {
    int err = 0;

    struct nfsfh *file_handle = NULL;
    err = fd_begin_op_file(fd, &file_handle);
    if (err) {
        dlog("invalid fd");
        goto fail;
    }

    err = nfs_fsync_async(nfs, file_handle, fsync_cb, NULL);
    if (err) {
        dlog("failed to enqueue command");
        goto fail;
    }

    return;

fail:
    memcpy(client_share, &err, sizeof err);
    microkit_notify(CLIENT_CHANNEL);
}

void mkdir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    dlogp(status != 0, "failed to write to file: %d (%s)", status, data);
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_mkdir(const char *path) {
    int err = nfs_mkdir_async(nfs, path, mkdir_cb, NULL);
    if (err) {
        dlog("failed to enqueue command");
        microkit_notify(CLIENT_CHANNEL);
    }
}

void rmdir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    dlogp(status != 0, "failed to write to file: %d (%s)", status, data);
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_rmdir(const char *path) {
    int err = nfs_rmdir_async(nfs, path, rmdir_cb, NULL);
    if (err) {
        dlog("failed to enqueue command");
        memcpy(client_share, &err, 4);
        microkit_notify(CLIENT_CHANNEL);
    }
}

void opendir_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    struct nfsdir *dir = data;
    fd_t fd = (fd_t)private_data;

    if (status == 0) {
        fd_set_dir(fd, dir);
        memcpy(client_share + 4, &fd, 8);
    } else {
        dlog("failed to open directory: %d (%s)", status, data);
        fd_free(fd);
    }

    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_opendir(const char *path) {
    int err = 0;

    fd_t fd;
    err = fd_alloc(&fd);
    if (err) {
        dlog("no free fds");
        goto fail_alloc;
    }

    err = nfs_opendir_async(nfs, path, opendir_cb, (void *)fd);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    fd_free(fd);
fail_alloc:
    memcpy(client_share, &err, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_closedir(fd_t fd) {
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
        dlog("trying to close fd with outstanding operations: %d\n");
        goto fail;
    }

    nfs_closedir(nfs, dir_handle);
    fd_free(fd);

fail:
    memcpy(client_share, &err, sizeof err);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_readdir(fd_t fd) {
    struct nfsdir *dir_handle = NULL;
    int status = fd_begin_op_dir(fd, &dir_handle);
    if (status) {
        dlog("(%d) invalid fd", status);
        goto fail_begin;
    }

    struct nfsdirent *dirent = nfs_readdir(nfs, dir_handle);
    if (dirent == NULL) {
        dlog("failed to read dir");
        status = -1;
        goto fail_readdir;
    } else {
    }

    strcpy(client_share + 4, dirent->name);

fail_readdir:
    fd_end_op(fd);
fail_begin:
    memcpy(client_share, &status, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_seekdir(fd_t fd, long loc) {
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
    memcpy(client_share, &err, sizeof err);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_telldir(fd_t fd) {
    int err = 0;

    struct nfsdir *dir_handle = NULL;
    err = fd_begin_op_dir(fd, &dir_handle);
    if (err) {
        dlog("invalid fd");
        goto fail;
    }
    long loc = nfs_telldir(nfs, dir_handle);
    memcpy(client_share + 4, &loc, sizeof loc);
    fd_end_op(fd);

fail:
    memcpy(client_share, &err, sizeof err);
    microkit_notify(CLIENT_CHANNEL);
}

void handle_rewinddir(fd_t fd) {
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
    memcpy(client_share, &err, 4);
    microkit_notify(CLIENT_CHANNEL);
}

void nfs_protected(microkit_channel ch, microkit_msginfo msginfo) {
    switch (microkit_msginfo_get_label(msginfo)) {
    case SDDF_FS_CMD_STAT: {
        handle_stat64(client_share);
        break;
    }
    case SDDF_FS_CMD_OPEN: {
        handle_open(client_share, O_RDWR | O_CREAT, O_RDWR);
        break;
    }
    case SDDF_FS_CMD_CLOSE: {
        fd_t fd = (int)microkit_mr_get(0);
        handle_close(fd);
        break;
    }
    case SDDF_FS_CMD_PREAD: {
        fd_t fd = (int)microkit_mr_get(0);
        uint64_t nbyte = microkit_mr_get(1);
        uint64_t offset = microkit_mr_get(2);
        handle_pread(fd, nbyte, offset);
        break;
    }
    case SDDF_FS_CMD_PWRITE: {
        fd_t fd = (int)microkit_mr_get(0);
        uint64_t nbyte = microkit_mr_get(1);
        uint64_t offset = microkit_mr_get(2);
        handle_pwrite(fd, nbyte, offset);
        break;
    }
    case SDDF_FS_CMD_RENAME: {
        int oldpath_offset = microkit_mr_get(0);
        int newpath_offset = microkit_mr_get(1);
        const char *oldpath = client_share + oldpath_offset;
        const char *newpath = client_share + newpath_offset;
        handle_rename(oldpath, newpath);
        break;
    }
    case SDDF_FS_CMD_UNLINK: {
        handle_unlink(client_share);
        break;
    }
    case SDDF_FS_CMD_MKDIR: {
        handle_mkdir(client_share);
        break;
    }
    case SDDF_FS_CMD_RMDIR: {
        handle_rmdir(client_share);
        break;
    }
    case SDDF_FS_CMD_OPENDIR: {
        handle_opendir(client_share);
        break;
    }
    case SDDF_FS_CMD_CLOSEDIR: {
        fd_t fd = (fd_t)microkit_mr_get(0);
        handle_closedir(fd);
        break;
    }
    case SDDF_FS_CMD_READDIR: {
        fd_t fd = microkit_mr_get(0);
        handle_readdir(fd);
        break;
    }
    case SDDF_FS_CMD_FSYNC: {
        fd_t fd = (fd_t)microkit_mr_get(0);
        handle_fsync(fd);
        break;
    }
    case SDDF_FS_CMD_SEEKDIR: {
        fd_t fd = (fd_t)microkit_mr_get(0);
        long loc = (fd_t)microkit_mr_get(1);
        handle_seekdir(fd, loc);
        break;
    }
    case SDDF_FS_CMD_TELLDIR: {
        fd_t fd = (fd_t)microkit_mr_get(0);
        handle_telldir(fd);
        break;
    }
    case SDDF_FS_CMD_REWINDDIR: {
        fd_t fd = (fd_t)microkit_mr_get(0);
        handle_rewinddir(fd);
        break;
    }
    default:
        dlog("unknown ppcall operation");
        break;
    }
}
