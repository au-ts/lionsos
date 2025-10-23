/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <bits/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>

#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/config.h>

#define MAX_PATH_LEN 128

static int fs_server_fd_map[MAX_FDS];
static char fd_path[MAX_FDS][MAX_PATH_LEN];

static size_t file_write(const void *buf, size_t len, int fd) {
    ptrdiff_t write_buffer;
    fs_buffer_allocate(&write_buffer);

    memcpy(fs_buffer_ptr(write_buffer), buf, len);

    fs_cmpl_t completion;
    fd_entry_t *fd_entry = posix_fd_entry(fd);
    fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_WRITE,
                                                .params.file_write = {
                                                    .fd = fs_server_fd_map[fd],
                                                    .offset = fd_entry->file_ptr,
                                                    .buf.offset = write_buffer,
                                                    .buf.size = len,
                                                }});

    fs_buffer_free(write_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        return -1;
    }

    size_t written = completion.data.file_write.len_written;
    fd_entry->file_ptr += written;

    return written;
}

static size_t file_read(void *buf, size_t len, int fd) {
    ptrdiff_t read_buffer;
    fs_buffer_allocate(&read_buffer);

    fs_cmpl_t completion;
    fd_entry_t *fd_entry = posix_fd_entry(fd);
    fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_READ,
                                                .params.file_read = {
                                                    .fd = fs_server_fd_map[fd],
                                                    .offset = fd_entry->file_ptr,
                                                    .buf.offset = read_buffer,
                                                    .buf.size = len,
                                                }});

    if (completion.status != FS_STATUS_SUCCESS) {
        fs_buffer_free(read_buffer);
        return -1;
    }

    size_t read = completion.data.file_read.len_read;
    memcpy(buf, fs_buffer_ptr(read_buffer), read);
    fs_buffer_free(read_buffer);

    fd_entry->file_ptr += read;

    return read;
}

static int file_close(int fd) {
    fs_cmpl_t completion;
    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry->flags & O_DIRECTORY) {
        fs_command_blocking(&completion, (fs_cmd_t){
                                             .type = FS_CMD_DIR_CLOSE,
                                             .params.dir_close.fd = fs_server_fd_map[fd],
                                         });
    } else {
        fs_command_blocking(&completion, (fs_cmd_t){
                                             .type = FS_CMD_FILE_CLOSE,
                                             .params.file_close.fd = fs_server_fd_map[fd],
                                         });
    }

    if (completion.status != FS_STATUS_SUCCESS) {
        return -1;
    }

    fs_server_fd_map[fd] = -1;
    memset(fd_path[fd], 0, MAX_PATH_LEN);

    return 0;
}

static int file_dup3(int oldfd, int newfd) {
    fs_server_fd_map[newfd] = fs_server_fd_map[oldfd];
    return 0;
}

static long sys_fcntl(va_list ap) {
    int fd = va_arg(ap, int);
    int op = va_arg(ap, int);

    switch (op) {
        case F_GETFL: {
            fd_entry_t *fd_entry = posix_fd_entry(fd);
            return fd_entry->flags;
        }
    }

    return 0;
}

static int fstat_int(const char *path, struct stat *statbuf) {
    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    assert(!err);

    ptrdiff_t output_buffer;
    err = fs_buffer_allocate(&output_buffer);
    assert(!err);

    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_STAT,
                                                .params.stat = {
                                                    .path.offset = path_buffer,
                                                    .path.size = path_len,
                                                    .buf.offset = output_buffer,
                                                    .buf.size = FS_BUFFER_SIZE,
                                                }});

    fs_buffer_free(path_buffer);

    if (completion.status == FS_STATUS_NO_FILE) {
        return -ENOENT;
    } else if (completion.status == FS_STATUS_INVALID_NAME) {
        return 0;
    } else if (completion.status != FS_STATUS_SUCCESS) {
        return -completion.status;
    }

    fs_stat_t *sb = fs_buffer_ptr(output_buffer);

    statbuf->st_dev = sb->dev;
    statbuf->st_ino = sb->ino;
    statbuf->st_mode = sb->mode;
    statbuf->st_nlink = sb->nlink;
    statbuf->st_uid = sb->uid;
    statbuf->st_gid = sb->gid;
    statbuf->st_rdev = sb->rdev;
    statbuf->st_size = sb->size;
    statbuf->st_blksize = sb->blksize;
    statbuf->st_blocks = sb->blocks;
    statbuf->st_atime = sb->atime;
    statbuf->st_mtime = sb->mtime;
    statbuf->st_ctime = sb->ctime;
    statbuf->st_atim.tv_nsec = sb->atime_nsec;
    statbuf->st_mtim.tv_nsec = sb->mtime_nsec;
    statbuf->st_ctim.tv_nsec = sb->ctime_nsec;

    fs_buffer_free(output_buffer);

    return 0;
}

static int file_fstat(int fd, struct stat *statbuf) {
    char *path = fd_path[fd];
    return fstat_int(path, statbuf);
}

static long sys_fstatat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *path = va_arg(ap, const char *);
    struct stat *statbuf = va_arg(ap, struct stat *);

    (void)dirfd;

    return fstat_int(path, statbuf);
}

static long sys_readlinkat(va_list ap) { return -EINVAL; }

static long sys_openat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *pathname = va_arg(ap, const char *);
    int flags = va_arg(ap, int);

    char path[MAX_PATH_LEN] = {0};

    // if pathname is absolute, the dirfd is ignored.
    if (pathname[0] != '/') {
        if (dirfd == AT_FDCWD) {
            // TODO: once we have cwd impl, use that instead
            path[0] = '/';
        } else {
            strncat(path, fd_path[dirfd], MAX_PATH_LEN);
            strncat(path, "/", 1);
        }
    }

    strncat(path, pathname, MAX_PATH_LEN);

    if (strcmp(path, "/etc/services") == 0) {
        return SERVICES_FD;
    }

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    assert(!err);

    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    uint64_t fs_flags = 0;
    if (flags & O_RDONLY) {
        fs_flags |= FS_OPEN_FLAGS_READ_ONLY;
    }
    if (flags & O_WRONLY) {
        fs_flags |= FS_OPEN_FLAGS_WRITE_ONLY;
    }
    if (flags & O_RDWR) {
        fs_flags |= FS_OPEN_FLAGS_READ_WRITE;
    }
    if (flags & O_CREAT) {
        fs_flags |= FS_OPEN_FLAGS_CREATE;
    }

    fs_cmpl_t completion;
    if (flags & O_DIRECTORY) {
        fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_DIR_OPEN,
                                                    .params.dir_open = {
                                                        .path.offset = path_buffer,
                                                        .path.size = path_len,
                                                    }});
    } else {
        fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_OPEN,
                                                    .params.file_open = {
                                                        .path.offset = path_buffer,
                                                        .path.size = path_len,
                                                        .flags = fs_flags,
                                                    }});
    }

    fs_buffer_free(path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        return -1;
    }

    uint64_t fs_fd;
    if (flags & O_DIRECTORY) {
        fs_fd = completion.data.dir_open.fd;
    } else {
        fs_fd = completion.data.file_open.fd;

        if (flags & O_TRUNC) {
            fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_TRUNCATE,
                                                        .params.file_truncate = {.fd = fs_fd, .length = 0}});
        }

        if (completion.status != FS_STATUS_SUCCESS) {
            return -1;
        }
    }

    int fd = posix_fd_allocate();
    assert(fs_server_fd_map[fd] == -1);
    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry != NULL) {
        *fd_entry = (fd_entry_t){.read = file_read,
                                 .write = file_write,
                                 .close = file_close,
                                 .dup3 = file_dup3,
                                 .fstat = file_fstat,
                                 .flags = flags,
                                 .file_ptr = 0};

        fs_server_fd_map[fd] = fs_fd;
    } else {
        return -1;
    }

    strcpy(fd_path[fd], path);

    return fd;
}

static long sys_lseek(va_list ap) {
    long fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    int whence = va_arg(ap, int);

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    /* TODO: need to sanitise input */
    size_t curr_fp = fd_entry->file_ptr;
    size_t new_fp;
    switch (whence) {
        case SEEK_SET:
            new_fp = offset;
            break;
        case SEEK_CUR:
            new_fp = curr_fp + offset;
            break;
        case SEEK_END: {
            fs_cmpl_t completion;
            int err = fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_SIZE,
                                                                  .params.file_size = {
                                                                      .fd = fs_server_fd_map[fd],
                                                                  }});
            if (err || completion.status != FS_STATUS_SUCCESS) {
                return -1;
            }
            new_fp = completion.data.file_size.size + offset;
            break;
        }
        default:
            printf("POSIX ERROR: lseek got unsupported whence %d\n", whence);
            // TODO: correct error
            return -1;
    }

    fd_entry->file_ptr = new_fp;
    return new_fp;
}

static long sys_mkdirat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *pathname = va_arg(ap, const char *);
    mode_t mode = va_arg(ap, mode_t);

    //TODO
    (void)mode;

    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    assert(!err);

    // TODO: factor out path resolution common between this and openat
    char path[MAX_PATH_LEN] = {0};

    // if pathname is absolute, the dirfd is ignored.
    if (pathname[0] != '/') {
        if (dirfd == AT_FDCWD) {
            // TODO: once we have cwd impl, use that instead
            path[0] = '/';
        } else {
            strncat(path, fd_path[dirfd], MAX_PATH_LEN);
            strncat(path, "/", 1);
        }
    }

    strncat(path, pathname, MAX_PATH_LEN);
    
    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len + 1);

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_DIR_CREATE,
                                                .params.dir_create = {
                                                    .path.offset = path_buffer,
                                                    .path.size = path_len,
                                                }});
    fs_buffer_free(path_buffer);

    if (completion.status != FS_STATUS_SUCCESS) {
        return -1;
    }

    return 0;
}

void libc_init_file() {
    libc_define_syscall(__NR_fcntl, sys_fcntl);
    libc_define_syscall(__NR_newfstatat, sys_fstatat);
    libc_define_syscall(__NR_readlinkat, sys_readlinkat);
    libc_define_syscall(__NR_openat, sys_openat);
    libc_define_syscall(__NR_lseek, sys_lseek);
    libc_define_syscall(__NR_mkdirat, sys_mkdirat);

    memset(fs_server_fd_map, -1, sizeof(fs_server_fd_map));
    memset(fd_path, 0, sizeof(fd_path));
}
