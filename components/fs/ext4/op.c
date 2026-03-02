/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "decl.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <libmicrokitco.h>

#include <sddf/blk/queue.h>

#include <ext4.h>
#include <ext4_errno.h>

#include <lions/fs/protocol.h>
#include <lions/fs/server.h>

/* Needed for flush */
extern blk_queue_handle_t blk_queue;
extern bool blk_request_pushed;

bool fs_initialised;

static ext4_file files[EXT4_MAX_OPENED_FILENUM];
static bool file_used[EXT4_MAX_OPENED_FILENUM];

typedef struct ext4_dir_handle {
    ext4_dir dir;
    uint64_t location;
} ext4_dir_handle_t;

static ext4_dir_handle_t dirs[EXT4_MAX_OPENED_DIRNUM];
static bool dir_used[EXT4_MAX_OPENED_DIRNUM];

_Static_assert(EXT4_MAX_OPENED_FILENUM <= MAX_OPEN_FILES, "EXT4_MAX_OPENED_FILENUM must not exceed MAX_OPEN_FILES");
_Static_assert(EXT4_MAX_OPENED_DIRNUM <= MAX_OPEN_FILES, "EXT4_MAX_OPENED_DIRNUM must not exceed MAX_OPEN_FILES");
_Static_assert((EXT4_MAX_OPENED_FILENUM + EXT4_MAX_OPENED_DIRNUM) <= MAX_OPEN_FILES,
               "ext4 open object pools must fit within MAX_OPEN_FILES");

/* Data shared with client */
extern char *fs_share;

extern struct ext4_blockdev *ext4_blockdev_get(void);

static ext4_file *file_alloc(void) {
    for (int i = 0; i < EXT4_MAX_OPENED_FILENUM; i++) {
        if (!file_used[i]) {
            file_used[i] = true;
            return &files[i];
        }
    }
    return NULL;
}

static void file_free(ext4_file *file) {
    uint32_t i = file - files;
    assert(file_used[i]);
    file_used[i] = false;
}

static ext4_dir_handle_t *dir_alloc(void) {
    for (int i = 0; i < EXT4_MAX_OPENED_DIRNUM; i++) {
        if (!dir_used[i]) {
            dir_used[i] = true;
            return &dirs[i];
        }
    }
    return NULL;
}

static void dir_free(ext4_dir_handle_t *dir) {
    uint32_t i = dir - dirs;
    assert(dir_used[i]);
    dir_used[i] = false;
}

static bool has_open_descriptors(void) {
    for (int i = 0; i < EXT4_MAX_OPENED_FILENUM; i++) {
        if (file_used[i]) {
            return true;
        }
    }

    for (int i = 0; i < EXT4_MAX_OPENED_DIRNUM; i++) {
        if (dir_used[i]) {
            return true;
        }
    }

    return false;
}

static uint64_t ext4_error_to_fs_status(int err) {
    switch (err) {
    case EOK:
        return FS_STATUS_SUCCESS;
    case ENOENT:
        return FS_STATUS_NO_FILE;
    case ENOTDIR:
        return FS_STATUS_NOT_DIRECTORY;
    case EEXIST:
        return FS_STATUS_ALREADY_EXISTS;
    case ENOTEMPTY:
        return FS_STATUS_NOT_EMPTY;
    case ENOSPC:
        return FS_STATUS_DIRECTORY_IS_FULL;
    case ENOMEM:
        return FS_STATUS_ALLOCATION_ERROR;
    case EROFS:
    case EACCES:
    case EPERM:
        return FS_STATUS_SERVER_WAS_DENIED;
    case EINVAL:
        return FS_STATUS_INVALID_NAME;
    default:
        return FS_STATUS_ERROR;
    }
}

static int map_fs_flags_to_ext4_flags(uint64_t fs_flags) {
    int ext4_flags = 0;

    switch (fs_flags & 0x3) {
    case FS_OPEN_FLAGS_READ_ONLY:
        ext4_flags |= O_RDONLY;
        break;
    case FS_OPEN_FLAGS_WRITE_ONLY:
        ext4_flags |= O_WRONLY;
        break;
    case FS_OPEN_FLAGS_READ_WRITE:
        ext4_flags |= O_RDWR;
        break;
    default:
        ext4_flags |= O_RDONLY;
        break;
    }

    if (fs_flags & FS_OPEN_FLAGS_CREATE) {
        ext4_flags |= O_CREAT;
    }

    return ext4_flags;
}

static int normalise_path(char *path) {
    size_t read_i = 0;
    size_t write_i = 0;
    bool prev_was_slash = false;

    while (path[read_i] != '\0') {
        char c = path[read_i++];
        if (c == '/') {
            if (prev_was_slash) {
                continue;
            }
            prev_was_slash = true;
        } else {
            prev_was_slash = false;
        }

        if (write_i >= FS_MAX_PATH_LENGTH) {
            return -1;
        }
        path[write_i++] = c;
    }

    if (write_i == 0) {
        path[0] = '/';
        path[1] = '\0';
        return 0;
    }

    if (write_i > 1 && path[write_i - 1] == '/') {
        write_i--;
    }

    path[write_i] = '\0';
    return 0;
}

static int copy_client_path_ext4(char *dest, fs_buffer_t path) {
    char raw_path[FS_MAX_PATH_LENGTH + 1];
    int err = fs_copy_client_path(raw_path, fs_share, EXT4_FS_DATA_REGION_SIZE, path);
    if (err) {
        return -1;
    }

    if (raw_path[0] == '\0') {
        memcpy(dest, EXT4_MOUNT_POINT, sizeof(EXT4_MOUNT_POINT));
    } else {
        size_t len = strlen(raw_path);
        if (len > FS_MAX_PATH_LENGTH) {
            return -1;
        }

        if (raw_path[0] == '/') {
            memcpy(dest, raw_path, len + 1);
        } else {
            if (len >= FS_MAX_PATH_LENGTH) {
                return -1;
            }
            dest[0] = '/';
            memcpy(dest + 1, raw_path, len + 1);
        }
    }

    return normalise_path(dest);
}

static bool ext4_direntry_is_dots(const ext4_direntry *entry) {
    return (entry->name_length == 1 && entry->name[0] == '.')
        || (entry->name_length == 2 && entry->name[0] == '.' && entry->name[1] == '.');
}

static const ext4_direntry *next_visible_dir_entry(ext4_dir_handle_t *dir_handle) {
    while (true) {
        const ext4_direntry *entry = ext4_dir_entry_next(&dir_handle->dir);
        if (entry == NULL || !ext4_direntry_is_dots(entry)) {
            return entry;
        }
    }
}

void handle_initialise(void) {
    co_data_t *args = microkit_cothread_my_arg();

    LOG_EXT4FS("Mounting file system!\n");

    if (fs_initialised) {
        args->status = FS_STATUS_ERROR;
        return;
    }

    struct ext4_blockdev *bdev = ext4_blockdev_get();
    int ret = ext4_device_register(bdev, EXT4_BLOCK_DEVICE_NAME);
    if (ret != EOK) {
        LOG_EXT4FS("ext4_device_register failed: %d\n", ret);
        args->status = ext4_error_to_fs_status(ret);
        return;
    }

    ret = ext4_mount(EXT4_BLOCK_DEVICE_NAME, EXT4_MOUNT_POINT, false);
    if (ret != EOK) {
        LOG_EXT4FS("ext4_mount failed: %d\n", ret);
        ext4_device_unregister(EXT4_BLOCK_DEVICE_NAME);
        args->status = ext4_error_to_fs_status(ret);
        return;
    }

    /*
     * Recover any incomplete journal transactions from an unclean shutdown,
     * then start the journal so subsequent operations are transactional.
     * ext4_recover returns ENOTSUP on filesystems without has_journal; that
     * is treated as success. ext4_journal_start is transparent and no-ops
     * on non-journaled images.
     */
    ret = ext4_recover(EXT4_MOUNT_POINT);
    if (ret != EOK && ret != ENOTSUP) {
        LOG_EXT4FS("ext4_recover failed: %d\n", ret);
        ext4_umount(EXT4_MOUNT_POINT);
        ext4_device_unregister(EXT4_BLOCK_DEVICE_NAME);
        args->status = ext4_error_to_fs_status(ret);
        return;
    }
    LOG_EXT4FS("ext4_recover result: %d\n", ret);

    ret = ext4_journal_start(EXT4_MOUNT_POINT);
    if (ret != EOK) {
        LOG_EXT4FS("ext4_journal_start failed: %d\n", ret);
        ext4_umount(EXT4_MOUNT_POINT);
        ext4_device_unregister(EXT4_BLOCK_DEVICE_NAME);
        args->status = ext4_error_to_fs_status(ret);
        return;
    }
    LOG_EXT4FS("ext4_journal_start result: %d\n", ret);

    fs_initialised = true;
    memset(file_used, 0, sizeof(file_used));
    memset(dir_used, 0, sizeof(dir_used));
    LOG_EXT4FS("Mounting file system result: %d\n", ret);
    args->status = FS_STATUS_SUCCESS;
}

void handle_deinitialise(void) {
    co_data_t *args = microkit_cothread_my_arg();

    LOG_EXT4FS("Unmounting file system!\n");

    if (!fs_initialised) {
        args->status = FS_STATUS_ERROR;
        return;
    }
    if (has_open_descriptors()) {
        args->status = FS_STATUS_OUTSTANDING_OPERATIONS;
        return;
    }

    int first_error = EOK;

    /* Stop the journal before flushing and unmounting. */
    int ret = ext4_journal_stop(EXT4_MOUNT_POINT);
    if (ret != EOK && first_error == EOK) {
        first_error = ret;
    }

    /* Flush cached data and tear down mount/device state. */
    ret = ext4_cache_flush(EXT4_MOUNT_POINT);
    if (ret != EOK && first_error == EOK) {
        first_error = ret;
    }

    ret = ext4_umount(EXT4_MOUNT_POINT);
    if (ret != EOK && first_error == EOK) {
        first_error = ret;
    }

    ret = ext4_device_unregister(EXT4_BLOCK_DEVICE_NAME);
    if (ret != EOK && first_error == EOK) {
        first_error = ret;
    }

    if (first_error == EOK) {
        fs_initialised = false;
        LOG_EXT4FS("Unmounting file system result: %d\n", first_error);
        args->status = FS_STATUS_SUCCESS;
        return;
    }

    LOG_EXT4FS("Unmounting file system result: %d\n", first_error);
    args->status = ext4_error_to_fs_status(first_error);
}

void handle_file_open(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t path = args->params.file_open.path;
    uint64_t open_flags = args->params.file_open.flags;

    char filepath[FS_MAX_PATH_LENGTH + 2];
    int err = copy_client_path_ext4(filepath, path);
    if (err) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    LOG_EXT4FS("ext4_open: file path: %s\n", filepath);
    LOG_EXT4FS("ext4_open: fs_protocol open flags: %lu\n", open_flags);
    LOG_EXT4FS("ext4_open: ext4 open flags: %d\n", map_fs_flags_to_ext4_flags(open_flags));

    ext4_file *file = file_alloc();
    if (file == NULL) {
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    int ret = ext4_fopen2(file, filepath, map_fs_flags_to_ext4_flags(open_flags));
    if (ret != EOK) {
        LOG_EXT4FS("ext4_open: error: %d\n", ret);
        file_free(file);
        args->status = ext4_error_to_fs_status(ret);
        return;
    }

    fd_t fd;
    err = fd_alloc(&fd);
    if (err) {
        ext4_fclose(file);
        file_free(file);
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    err = fd_set_file(fd, file);
    assert(!err);

    args->status = FS_STATUS_SUCCESS;
    args->result.file_open.fd = fd;
}

void handle_file_write(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_write.fd;
    fs_buffer_t buffer = args->params.file_write.buf;
    uint64_t offset = args->params.file_write.offset;

    LOG_EXT4FS("ext4_write: bytes to write: %lu, write offset: %lu\n", buffer.size, offset);

    char *data = fs_get_client_buffer(fs_share, EXT4_FS_DATA_REGION_SIZE, buffer);
    if (data == NULL) {
        LOG_EXT4FS("ext4_write: invalid buffer\n");
        args->result.file_write.len_written = 0;
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    ext4_file *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_EXT4FS("ext4_write: invalid fd: %d\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    int ret = ext4_fseek(file, offset, SEEK_SET);
    size_t written = 0;
    if (ret == EOK) {
        ret = ext4_fwrite(file, data, buffer.size, &written);
    }

    fd_end_op(fd);

    if (ret == EOK) {
        LOG_EXT4FS("ext4_write: bytes written: %zu\n", written);
    } else {
        LOG_EXT4FS("ext4_write: error: %d\n", ret);
    }

    args->status = ext4_error_to_fs_status(ret);
    args->result.file_write.len_written = written;
}

void handle_file_read(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_read.fd;
    fs_buffer_t buffer = args->params.file_read.buf;
    uint64_t offset = args->params.file_read.offset;

    LOG_EXT4FS("ext4_read: bytes to read: %lu, read offset: %lu\n", buffer.size, offset);

    char *data = fs_get_client_buffer(fs_share, EXT4_FS_DATA_REGION_SIZE, buffer);
    if (data == NULL) {
        LOG_EXT4FS("ext4_read: invalid buffer provided\n");
        args->result.file_read.len_read = 0;
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    ext4_file *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_EXT4FS("ext4_read: invalid fd: %d\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    int ret = ext4_fseek(file, offset, SEEK_SET);
    size_t read = 0;
    if (ret == EOK) {
        ret = ext4_fread(file, data, buffer.size, &read);
    }

    fd_end_op(fd);

    if (ret == EOK) {
        LOG_EXT4FS("ext4_read: bytes read: %zu\n", read);
    } else {
        LOG_EXT4FS("ext4_read: error: %d\n", ret);
    }

    args->status = ext4_error_to_fs_status(ret);
    args->result.file_read.len_read = read;
}

void handle_file_close(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_close.fd;

    ext4_file *file;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_EXT4FS("ext4_close: invalid file descriptor\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        LOG_EXT4FS("ext4_close: fd has outstanding operations\n");
        args->status = FS_STATUS_OUTSTANDING_OPERATIONS;
        return;
    }

    int ret = ext4_fclose(file);
    if (ret == EOK) {
        file_free(file);
        fd_free(fd);
    } else {
        fd_set_file(fd, file);
    }

    args->status = ext4_error_to_fs_status(ret);
}

void handle_stat(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t path = args->params.stat.path;
    fs_buffer_t output_buffer = args->params.stat.buf;
    uint64_t size = args->params.stat.buf.size;

    fs_stat_t *file_stat = fs_get_client_buffer(fs_share, EXT4_FS_DATA_REGION_SIZE, output_buffer);
    if (file_stat == NULL || size < sizeof(fs_stat_t)) {
        LOG_EXT4FS("ext4_stat: invalid output buffer provided\n");
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    char filepath[FS_MAX_PATH_LENGTH + 2];
    int err = copy_client_path_ext4(filepath, path);
    if (err) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    LOG_EXT4FS("ext4_stat: asking for filename: %s\n", filepath);

    struct ext4_inode inode;
    uint32_t inode_num = 0;
    int ret = ext4_raw_inode_fill(filepath, &inode_num, &inode);
    if (ret != EOK) {
        args->status = ext4_error_to_fs_status(ret);
        return;
    }

    memset(file_stat, 0, sizeof(fs_stat_t));
    file_stat->ino = inode_num;
    file_stat->mode = inode.mode;
    file_stat->nlink = inode.links_count;
    file_stat->uid = ((uint32_t)inode.osd2.linux2.uid_high << 16) | inode.uid;
    file_stat->gid = ((uint32_t)inode.osd2.linux2.gid_high << 16) | inode.gid;
    file_stat->size = ((uint64_t)inode.size_hi << 32) | inode.size_lo;
    file_stat->blksize = ext4_blockdev_get()->bdif->ph_bsize;
    file_stat->blocks = ((uint64_t)inode.osd2.linux2.blocks_high << 32) | inode.blocks_count_lo;
    file_stat->atime = inode.access_time;
    file_stat->mtime = inode.modification_time;
    file_stat->ctime = inode.change_inode_time;

    args->status = FS_STATUS_SUCCESS;
}

void handle_file_size(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.file_size.fd;

    ext4_file *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_EXT4FS("ext4_size: invalid fd: %d\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    uint64_t size = ext4_fsize(file);
    fd_end_op(fd);

    args->status = FS_STATUS_SUCCESS;
    args->result.file_size.size = size;
}

void handle_rename(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t oldpath_buffer = args->params.rename.old_path;
    fs_buffer_t newpath_buffer = args->params.rename.new_path;

    char oldpath[FS_MAX_PATH_LENGTH + 2];
    char newpath[FS_MAX_PATH_LENGTH + 2];

    int err = copy_client_path_ext4(oldpath, oldpath_buffer);
    if (err) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }
    err = copy_client_path_ext4(newpath, newpath_buffer);
    if (err) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    int ret = ext4_frename(oldpath, newpath);
    if (ret == EISDIR || ret == ENOTDIR) {
        ret = ext4_dir_mv(oldpath, newpath);
    }

    args->status = ext4_error_to_fs_status(ret);
}

void handle_file_remove(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.file_remove.path;

    char filepath[FS_MAX_PATH_LENGTH + 2];
    int err = copy_client_path_ext4(filepath, buffer);
    if (err) {
        LOG_EXT4FS("ext4_unlink: invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    int ret = ext4_fremove(filepath);
    args->status = ext4_error_to_fs_status(ret);
}

void handle_file_truncate(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.file_truncate.fd;
    uint64_t len = args->params.file_truncate.length;

    ext4_file *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_EXT4FS("ext4_truncate: invalid fd\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    int ret = ext4_ftruncate(file, len);
    fd_end_op(fd);

    args->status = ext4_error_to_fs_status(ret);
}

void handle_dir_create(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.dir_create.path;

    char dirpath[FS_MAX_PATH_LENGTH + 2];
    int err = copy_client_path_ext4(dirpath, buffer);
    if (err) {
        LOG_EXT4FS("ext4_mkdir: invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    int ret = ext4_dir_mk(dirpath);
    args->status = ext4_error_to_fs_status(ret);
}

void handle_dir_remove(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.dir_remove.path;

    char dirpath[FS_MAX_PATH_LENGTH + 2];
    int err = copy_client_path_ext4(dirpath, buffer);
    if (err) {
        LOG_EXT4FS("ext4_rmdir: invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    /*
     * ext4_dir_rm returns ENOTEMPTY if the directory is not empty, which
     * ext4_error_to_fs_status maps to FS_STATUS_NOT_EMPTY.
     */
    int ret = ext4_dir_rm(dirpath);
    args->status = ext4_error_to_fs_status(ret);
}

void handle_dir_open(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.dir_open.path;

    char dirpath[FS_MAX_PATH_LENGTH + 2];
    int err = copy_client_path_ext4(dirpath, buffer);
    if (err) {
        LOG_EXT4FS("ext4_opendir: invalid buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    LOG_EXT4FS("ext4 opendir directory path: %s\n", dirpath);

    ext4_dir_handle_t *dir = dir_alloc();
    if (dir == NULL) {
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    int ret = ext4_dir_open(&dir->dir, dirpath);
    if (ret != EOK) {
        dir_free(dir);
        if (ret == ENOENT || ret == ENOTDIR) {
            args->status = FS_STATUS_NOT_DIRECTORY;
        } else {
            args->status = ext4_error_to_fs_status(ret);
        }
        return;
    }
    dir->location = 0;

    fd_t fd;
    err = fd_alloc(&fd);
    if (err) {
        ext4_dir_close(&dir->dir);
        dir_free(dir);
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    err = fd_set_dir(fd, dir);
    assert(!err);

    args->status = FS_STATUS_SUCCESS;
    args->result.dir_open.fd = fd;
}

void handle_dir_read(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.dir_read.fd;
    fs_buffer_t buffer = args->params.dir_read.buf;
    uint64_t size = args->params.dir_read.buf.size;

    LOG_EXT4FS("ext4 readdir file descriptor: %lu\n", fd);

    char *name = fs_get_client_buffer(fs_share, EXT4_FS_DATA_REGION_SIZE, buffer);
    if (name == NULL) {
        LOG_EXT4FS("ext4_readdir: invalid buffer\n");
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    ext4_dir_handle_t *dir_handle = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir_handle);
    if (err) {
        LOG_EXT4FS("ext4_readdir: invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    const ext4_direntry *entry = next_visible_dir_entry(dir_handle);
    if (entry == NULL) {
        fd_end_op(fd);
        args->status = FS_STATUS_END_OF_DIRECTORY;
        return;
    }

    uint64_t len = entry->name_length;
    if (size < len) {
        fd_end_op(fd);
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    memcpy(name, entry->name, len);
    if (size > len) {
        name[len] = '\0';
    }
    dir_handle->location++;

    fd_end_op(fd);

    LOG_EXT4FS("ext4 readdir file name: %.*s\n", (uint32_t)len, (char *)name);
    args->result.dir_read.path_len = len;
    args->status = FS_STATUS_SUCCESS;
}

void handle_dir_tell(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.dir_tell.fd;

    ext4_dir_handle_t *dir_handle = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir_handle);
    if (err) {
        LOG_EXT4FS("ext4_telldir: invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    args->result.dir_tell.location = dir_handle->location;
    fd_end_op(fd);

    args->status = FS_STATUS_SUCCESS;
}

void handle_dir_rewind(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.dir_rewind.fd;

    ext4_dir_handle_t *dir_handle = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir_handle);
    if (err) {
        LOG_EXT4FS("ext4_rewinddir: invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    ext4_dir_entry_rewind(&dir_handle->dir);
    dir_handle->location = 0;
    fd_end_op(fd);

    args->status = FS_STATUS_SUCCESS;
}

void handle_file_sync(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_sync.fd;

    /* Validate the fd refers to a real open file. */
    ext4_file *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_EXT4FS("ext4_sync: invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }
    fd_end_op(fd);

    /*
     * lwext4 has no per-file fsync. In the default write-through mode every
     * ext4_fwrite already pushes data blocks to the block device immediately;
     * this flush forces any cached metadata (inodes, bitmaps) to disk.
     */
    int ret = ext4_cache_flush(EXT4_MOUNT_POINT);
    LOG_EXT4FS("ext4_sync: cache flush result: %d\n", ret);

    /*
     * Issue a BLK_REQ_FLUSH so the block virtualiser/driver commits any
     * write-back caches to persistent storage.
     */
    if (ret == EOK) {
        err = blk_enqueue_req(&blk_queue, BLK_REQ_FLUSH, 0, 0, 0, microkit_cothread_my_handle());
        if (!err) {
            blk_request_pushed = true;
            wait_for_blk_resp();
            blk_resp_status_t status = (blk_resp_status_t)(uintptr_t)microkit_cothread_my_arg();
            if (status != BLK_RESP_OK) {
                LOG_EXT4FS("ext4_sync: BLK_REQ_FLUSH failed: %d\n", status);
                ret = EIO;
            }
        } else {
            LOG_EXT4FS("ext4_sync: blk_enqueue_req failed\n");
            ret = EIO;
        }
    }

    args->status = ext4_error_to_fs_status(ret);
}

void handle_dir_close(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.dir_close.fd;

    ext4_dir_handle_t *dir_handle = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir_handle);
    if (err) {
        LOG_EXT4FS("ext4_closedir: invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        LOG_EXT4FS("ext4_closedir: trying to close fd with outstanding operations\n");
        args->status = FS_STATUS_OUTSTANDING_OPERATIONS;
        return;
    }

    int ret = ext4_dir_close(&dir_handle->dir);
    if (ret == EOK) {
        fd_free(fd);
        dir_free(dir_handle);
    } else {
        fd_set_dir(fd, dir_handle);
    }

    args->status = ext4_error_to_fs_status(ret);
}

void handle_dir_seek(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.dir_seek.fd;
    int64_t loc = args->params.dir_seek.loc;

    if (loc < 0) {
        args->status = FS_STATUS_ERROR;
        return;
    }

    ext4_dir_handle_t *dir_handle = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir_handle);
    if (err) {
        LOG_EXT4FS("ext4_seekdir: invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    ext4_dir_entry_rewind(&dir_handle->dir);
    dir_handle->location = 0;

    for (int64_t i = 0; i < loc; i++) {
        const ext4_direntry *entry = next_visible_dir_entry(dir_handle);
        if (entry == NULL) {
            fd_end_op(fd);
            args->status = FS_STATUS_ERROR;
            return;
        }
        dir_handle->location++;
    }

    fd_end_op(fd);
    args->status = FS_STATUS_SUCCESS;
}
