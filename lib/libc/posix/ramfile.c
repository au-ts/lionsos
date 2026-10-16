/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Files go to the in-process ramfs (components/fs/ramfs) when RAM_FATFS is set; file.c otherwise.
#include <fat_config.h>

#ifdef RAM_FATFS

#include "lions/fs/protocol.h"
#include <assert.h>
#include <bits/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>

#include <lions/fs/ramfs.h>

#define FILE_SUCC 0
#define FILE_ERR  1

static int fs_server_fd_map[MAX_FDS];

static char fd_path[MAX_FDS][PATH_MAX];

static int resolve_path(int dirfd, const char *path, char *out_path, size_t out_size) {
    assert(out_size >= 2); // need at least space for "/" (null-terminated)

    if (path == NULL || out_path == NULL) {
        return EINVAL;
    }

    if (strlen(path) >= out_size || out_size > PATH_MAX) {
        return ENAMETOOLONG;
    }

    if (dirfd != AT_FDCWD && dirfd != ETC_FD && (dirfd < 0 || dirfd >= MAX_FDS)) {
        return EBADF;
    }

    if (path[0] == '/') {
        // Absolute path: ignore dirfd
        strncpy(out_path, path, out_size - 1);
        out_path[out_size - 1] = '\0';
    } else {
        // TODO: support modifying CWD
        if (dirfd == AT_FDCWD) {
            strncpy(out_path, "/", 2);
        } else if (dirfd == ETC_FD) {
            strncpy(out_path, "/etc/", out_size - 1);
            out_path[out_size - 1] = '\0';
        } else {
            size_t base_len = strlen(fd_path[dirfd]);

            // can we fit the base path + '/' ?
            if (base_len + 1 >= out_size) {
                return ENAMETOOLONG;
            }
            strncpy(out_path, fd_path[dirfd], out_size - 1);
            out_path[out_size - 1] = '\0';
            // Only append '/' if the base path doesn't already end with one
            if (base_len > 0 && fd_path[dirfd][base_len - 1] != '/') {
                strncat(out_path, "/", out_size - strlen(out_path) - 1);
            }
        }

        // Handle "." path - means current directory, so don't append anything
        if (strcmp(path, ".") == 0) {
            return FILE_SUCC;
        }

        // can we fit the subpath?
        if (strlen(out_path) + strlen(path) >= out_size) {
            return ENAMETOOLONG;
        }
        strncat(out_path, path, out_size - strlen(out_path) - 1);
    }

    return FILE_SUCC;
}

static size_t fs_status_to_errno[FS_STATUS_NUM_STATUSES] = {
    [FS_STATUS_SUCCESS] = FILE_SUCC,
    [FS_STATUS_ERROR] = FILE_ERR,
    [FS_STATUS_INVALID_BUFFER] = EINVAL,
    [FS_STATUS_INVALID_PATH] = ENOENT,
    [FS_STATUS_INVALID_FD] = EBADF,
    [FS_STATUS_ALLOCATION_ERROR] = ENOMEM,
    [FS_STATUS_OUTSTANDING_OPERATIONS] = EBUSY,
    [FS_STATUS_INVALID_NAME] = EINVAL,
    [FS_STATUS_TOO_MANY_OPEN_FILES] = EMFILE,
    [FS_STATUS_SERVER_WAS_DENIED] = EPERM,
    [FS_STATUS_INVALID_WRITE] = EACCES,
    [FS_STATUS_INVALID_READ] = EACCES,
    [FS_STATUS_DIRECTORY_IS_FULL] = ENOSPC,
    [FS_STATUS_INVALID_COMMAND] = EINVAL,
    [FS_STATUS_END_OF_DIRECTORY] = FILE_ERR,
    [FS_STATUS_NO_FILE] = ENOENT,
    [FS_STATUS_NOT_DIRECTORY] = ENOTDIR,
    [FS_STATUS_ALREADY_EXISTS] = EEXIST,
    [FS_STATUS_NOT_EMPTY] = ENOTEMPTY,
};

static ssize_t file_write(const void *buf, size_t len, int fd) {
    if (len == 0) {
        return 0;
    }

    if (buf == NULL) {
        return -EFAULT;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    uint64_t written;
    int status = handle_file_write(fs_server_fd_map[fd], buf, len, fd_entry->file_ptr, &written);
    if (status != FS_STATUS_SUCCESS) {
        return -fs_status_to_errno[status];
    }

    fd_entry->file_ptr += written;

    return written;
}

static ssize_t file_read(void *buf, size_t len, int fd) {
    if (len == 0) {
        return 0;
    }

    if (buf == NULL) {
        return -EFAULT;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    uint64_t total_read;
    int status = handle_file_read(fs_server_fd_map[fd], buf, len, fd_entry->file_ptr, &total_read);
    if (status != FS_STATUS_SUCCESS) {
        return -fs_status_to_errno[status];
    }

    fd_entry->file_ptr += total_read;

    return total_read;
}

static int file_close(int fd) {
    int status;
    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_entry->flags & O_DIRECTORY) {
        status = handle_dir_close(fs_server_fd_map[fd]);
    } else {
        status = handle_file_close(fs_server_fd_map[fd]);
    }

    // Always release the fd even if there is a close error
    fs_server_fd_map[fd] = -1;
    memset(fd_path[fd], 0, PATH_MAX);

    posix_fd_deallocate(fd);

    return -fs_status_to_errno[status];
}

static int file_dup3(int oldfd, int newfd) {
    // TODO: refcount of underlying file?
    fs_server_fd_map[newfd] = fs_server_fd_map[oldfd];
    return 0;
}

static int fstat_int(const char *path, struct stat *statbuf) {
    // FatFS rejects "/" with FR_INVALID_NAME, so return a minimal stat for it
    // here instead of forwarding to the FS server.
    if (strcmp(path, "/") == 0) {
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFDIR | 0755;
        return 0;
    }

    fs_stat_t sb;
    int status = handle_stat(path, &sb);
    if (status != FS_STATUS_SUCCESS) {
        return -fs_status_to_errno[status];
    }

    statbuf->st_dev = sb.dev;
    statbuf->st_ino = sb.ino;
    statbuf->st_mode = sb.mode;
    statbuf->st_nlink = sb.nlink;
    statbuf->st_uid = sb.uid;
    statbuf->st_gid = sb.gid;
    statbuf->st_rdev = sb.rdev;
    statbuf->st_size = sb.size;
    statbuf->st_blksize = sb.blksize;
    statbuf->st_blocks = sb.blocks;
    statbuf->st_atime = sb.atime;
    statbuf->st_mtime = sb.mtime;
    statbuf->st_ctime = sb.ctime;
    statbuf->st_atim.tv_nsec = sb.atime_nsec;
    statbuf->st_mtim.tv_nsec = sb.mtime_nsec;
    statbuf->st_ctim.tv_nsec = sb.ctime_nsec;

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

    if (statbuf == NULL) {
        return -EFAULT;
    }

    char full_path[PATH_MAX] = { 0 };
    int err = resolve_path(dirfd, path, full_path, PATH_MAX);
    if (err != FILE_SUCC) {
        return -err;
    }

    if (strcmp(full_path, "/etc") == 0) {
        // Return minimal stat for etc directory
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFDIR | 0555; // directory, read-only
        statbuf->st_nlink = 2;
        return 0;
    }

    if (strcmp(full_path, "/etc/services") == 0) {
        // Return minimal stat for services file
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFREG | 0444; // regular file, read-only
        statbuf->st_nlink = 1;
        statbuf->st_size = 0;
        return 0;
    }

    return fstat_int(full_path, statbuf);
}

static long sys_readlinkat(va_list ap) { return -EINVAL; }

static long sys_openat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *path = va_arg(ap, const char *);
    int flags = va_arg(ap, int);

    char full_path[PATH_MAX] = { 0 };
    int err = resolve_path(dirfd, path, full_path, PATH_MAX);
    if (err != FILE_SUCC) {
        return -err;
    }

    if (strcmp(full_path, "/etc") == 0) {
        if (flags & O_DIRECTORY) {
            return ETC_FD;
        }
        return -EISDIR;
    }

    if (strcmp(full_path, "/etc/services") == 0) {
        return SERVICES_FD;
    }

    // Allocate fd for newly opened file
    int fd = posix_fd_allocate();
    if (fd < 0) {
        return -EMFILE;
    }

    // We control this map, failure here means we haven't cleaned up properly.
    assert(fs_server_fd_map[fd] == -1);

    uint64_t fs_flags = 0;
    if (flags & O_WRONLY) {
        fs_flags |= FS_OPEN_FLAGS_WRITE_ONLY;
    } else if (flags & O_RDWR) {
        fs_flags |= FS_OPEN_FLAGS_READ_WRITE;
    } else {
        // O_RDONLY: typically 0 so don't check explicitly
        fs_flags |= FS_OPEN_FLAGS_READ_ONLY;
    }

    if (flags & O_CREAT) {
        fs_flags |= FS_OPEN_FLAGS_CREATE;
    }

    // O_CREAT|O_EXCL: fail if file already exists
    if ((flags & O_CREAT) && (flags & O_EXCL)) {
        struct stat statbuf;
        if (fstat_int(full_path, &statbuf) == 0) {
            posix_fd_deallocate(fd);
            return -EEXIST;
        }
    }

    // fail if opening a directory with O_WRONLY or O_RDWR without O_DIRECTORY
    if (!(flags & O_DIRECTORY) && (flags & (O_WRONLY | O_RDWR))) {
        struct stat statbuf;
        if (fstat_int(full_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            posix_fd_deallocate(fd);
            return -EISDIR;
        }
    }

    fd_t fs_fd;
    int status;
    if (flags & O_DIRECTORY) {
        status = handle_dir_open(full_path, &fs_fd);
    } else {
        status = handle_file_open(full_path, fs_flags, &fs_fd);
    }

    if (status != FS_STATUS_SUCCESS) {
        posix_fd_deallocate(fd);
        return -fs_status_to_errno[status];
    }

    if (!(flags & O_DIRECTORY) && (flags & O_TRUNC)) {
        status = handle_file_truncate(fs_fd, 0);
        if (status != FS_STATUS_SUCCESS) {
            handle_file_close(fs_fd);
            posix_fd_deallocate(fd);
            return -fs_status_to_errno[status];
        }
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry != NULL) {
        *fd_entry = (fd_entry_t) { .read = file_read,
                                   .write = file_write,
                                   .close = file_close,
                                   .dup3 = file_dup3,
                                   .fstat = file_fstat,
                                   .flags = flags,
                                   .file_ptr = 0 };

        fs_server_fd_map[fd] = fs_fd;
    } else {
        // Unreachable: we just allocated fd.
        assert(false);
        return -EBADF;
    }

    strncpy(fd_path[fd], full_path, PATH_MAX);

    return fd;
}

static long sys_lseek(va_list ap) {
    long fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    int whence = va_arg(ap, int);

    if (fd == SERVICES_FD || fd == ETC_FD) {
        return -EBADF;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    off_t curr_fp = fd_entry->file_ptr;
    off_t new_fp;
    switch (whence) {
    case SEEK_SET:
        if (offset < 0) {
            return -EINVAL;
        }
        new_fp = offset;
        break;
    case SEEK_CUR:
        // result would overflow off_t
        if ((off_t)(LONG_MAX - curr_fp) < offset) {
            return -EOVERFLOW;
        }
        // result would be negative
        if (offset < 0 && curr_fp < (size_t)(-offset)) {
            return -EINVAL;
        }
        new_fp = curr_fp + offset;
        break;
    case SEEK_END: {
        uint64_t size;
        int status = handle_file_size(fs_server_fd_map[fd], &size);
        if (status != FS_STATUS_SUCCESS) {
            return -fs_status_to_errno[status];
        }

        if ((off_t)(LONG_MAX - size) < offset) {
            return -EOVERFLOW;
        }
        new_fp = size + offset;
        break;
    }
    default:
        fprintf(stderr, "POSIX|ERROR: lseek got unsupported whence %d\n", whence);
        return -EINVAL;
    }

    fd_entry->file_ptr = new_fp;
    return new_fp;
}

static long sys_mkdirat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *path = va_arg(ap, const char *);
    mode_t mode = va_arg(ap, mode_t);

    //TODO
    (void)mode;

    char full_path[PATH_MAX] = { 0 };
    int err = resolve_path(dirfd, path, full_path, PATH_MAX);
    if (err != FILE_SUCC) {
        return -err;
    }

    int status = handle_dir_create(full_path);
    if (status != FS_STATUS_SUCCESS) {
        return -fs_status_to_errno[status];
    }

    return 0;
}

static long sys_unlinkat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *path = va_arg(ap, const char *);
    int flags = va_arg(ap, int);

    char full_path[PATH_MAX] = { 0 };
    int err = resolve_path(dirfd, path, full_path, PATH_MAX);
    if (err != FILE_SUCC) {
        return -err;
    }

    if (strcmp(full_path, "/etc/services") == 0) {
        return -EPERM;
    }

    if (!(flags & AT_REMOVEDIR)) {
        struct stat statbuf;
        int stat_err = fstat_int(full_path, &statbuf);
        if (stat_err == 0 && S_ISDIR(statbuf.st_mode)) {
            return -EISDIR;
        }
        // If stat returns error and we have a path with multiple components,
        // check if an intermediate component is not a directory
        if (stat_err != 0) {
            // Walk up the path checking each parent until we find one that exists
            char parent_path[PATH_MAX];
            strncpy(parent_path, full_path, PATH_MAX - 1);
            parent_path[PATH_MAX - 1] = '\0';
            char *last_slash = strrchr(parent_path, '/');
            while (last_slash != NULL && last_slash != parent_path) {
                *last_slash = '\0';
                int parent_stat_err = fstat_int(parent_path, &statbuf);
                if (parent_stat_err == 0) {
                    // fstat succeeded: this is a valid path
                    if (!S_ISDIR(statbuf.st_mode)) {
                        return -ENOTDIR;
                    }
                    break;  // It's a directory, stop checking
                }
                // fstat still failing - keep checking parents
                last_slash = strrchr(parent_path, '/');
            }
        }
    }

    int status;

    if (flags & AT_REMOVEDIR) {
        // Remove directory
        status = handle_dir_remove(full_path);
    } else {
        // TODO: refcounted unlink?
        status = handle_file_remove(full_path);
    }

    if (status != FS_STATUS_SUCCESS) {
        return -fs_status_to_errno[status];
    }

    return 0;
}

void libc_init_file() {
    libc_define_syscall(__NR_newfstatat, sys_fstatat);
    libc_define_syscall(__NR_readlinkat, sys_readlinkat);
    libc_define_syscall(__NR_openat, sys_openat);
    libc_define_syscall(__NR_lseek, sys_lseek);
    libc_define_syscall(__NR_mkdirat, sys_mkdirat);
    libc_define_syscall(__NR_unlinkat, sys_unlinkat);

    memset(fs_server_fd_map, -1, sizeof(fs_server_fd_map));
    memset(fd_path, 0, sizeof(fd_path));
}

#endif /* RAM_FATFS */
