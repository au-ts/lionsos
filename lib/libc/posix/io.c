/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <bits/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/uio.h>

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>

#include <stdio.h>

static long sys_write(va_list ap) {
    int fd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t count = va_arg(ap, size_t);

    if (count == 0) {
        return 0;
    }

    if (buf == NULL) {
        return -EFAULT;
    }

    if (fd == SERVICES_FD) {
        // Don't allow writes to services file
        return -EBADF;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    return fd_entry->write(buf, count, fd);
}

static long sys_read(va_list ap) {
    int fd = va_arg(ap, int);
    void *buf = va_arg(ap, void *);
    size_t count = va_arg(ap, size_t);

    if (count == 0) {
        return 0;
    }

    if (buf == NULL) {
        return -EFAULT;
    }

    if (fd == SERVICES_FD) {
        // Just return EOF to indicate no services available
        return 0;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    return fd_entry->read(buf, count, fd);
}

static long sys_writev(va_list ap) {
    int fd = va_arg(ap, int);
    struct iovec *iov = va_arg(ap, struct iovec *);
    int iovcnt = va_arg(ap, int);

    if (iov == NULL) {
        return -EFAULT;
    }

    if (fd == SERVICES_FD) {
        // Don't allow writes to services file
        return -EBADF;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    long long sum = 0;

    /* The iovcnt argument is valid if greater than 0 and less than or equal to IOV_MAX. */
    if (iovcnt <= 0 || iovcnt > IOV_MAX) {
        return -EINVAL;
    }

    /* The sum of iov_len is valid if less than or equal to SSIZE_MAX i.e. cannot overflow
       a ssize_t. */
    for (int i = 0; i < iovcnt; i++) {
        sum += (long long)iov[i].iov_len;
        if (sum > SSIZE_MAX) {
            return -EINVAL;
        }
    }

    /* If all the iov_len members in the array are 0, return 0. */
    if (!sum) {
        return 0;
    }

    ssize_t ret = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) {
            continue;
        }

        if (iov[i].iov_base == NULL) {
            return -EFAULT;
        }

        ssize_t written = fd_entry->write(iov[i].iov_base, iov[i].iov_len, fd);

        if (written < 0) {
            return written;
        }

        ret += written;

        // Return immediately on short writes.
        if (written < iov[i].iov_len) {
            break;
        }
    }

    return ret;
}

static long sys_readv(va_list ap) {
    int fd = va_arg(ap, int);
    const struct iovec *iov = va_arg(ap, const struct iovec *);
    int iovcnt = va_arg(ap, int);

    if (iov == NULL) {
        return -EFAULT;
    }

    if (fd == SERVICES_FD) {
        // Just return EOF to indicate no services available
        return 0;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    long long sum = 0;

    /* The iovcnt argument is valid if greater than 0 and less than or equal to IOV_MAX. */
    if (iovcnt <= 0 || iovcnt > IOV_MAX) {
        return -EINVAL;
    }

    /* The sum of iov_len is valid if less than or equal to SSIZE_MAX i.e. cannot overflow
       a ssize_t. */
    for (int i = 0; i < iovcnt; i++) {
        sum += (long long)iov[i].iov_len;
        if (sum > SSIZE_MAX) {
            return -EINVAL;
        }
    }

    ssize_t ret = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) {
            continue;
        }

        if (iov[i].iov_base == NULL) {
            return -EFAULT;
        }

        ssize_t read = fd_entry->read(iov[i].iov_base, iov[i].iov_len, fd);

        if (read < 0) {
            return read;
        }

        ret += read;

        // Return immediately on short reads.
        if (read < iov[i].iov_len) {
            break;
        }
    }

    return ret;
}

static long sys_close(va_list ap) {
    long fd = va_arg(ap, int);

    if (fd == SERVICES_FD) {
        return 0;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    int err = fd_entry->close(fd);
    if (err) {
        return err;
    }

    return 0;
}

static long sys_ioctl(va_list ap) {
    int fd = va_arg(ap, int);
    int op = va_arg(ap, int);
    (void)op;

    if (fd == SERVICES_FD) {
        // /etc/services does not support ioctl
        return -ENOTTY;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    /* muslc does some ioctls to stdout, so just allow these to silently go through */
    if (fd == STDOUT_FD) {
        return 0;
    }

    return -EINVAL;
}

static long sys_dup3(va_list ap) {
    int oldfd = va_arg(ap, int);
    int newfd = va_arg(ap, int);
    int flags = va_arg(ap, int);

    if (newfd < 0 || newfd >= MAX_FDS) {
        return -EBADF;
    }

    // Reject invalid flags
    if (flags & ~O_CLOEXEC) {
        return -EINVAL;
    }

    if (oldfd == SERVICES_FD || newfd == SERVICES_FD) {
        // Don't allow duping to or from services file
        return -EBADF;
    }

    fd_entry_t *oldfd_entry = posix_fd_entry(oldfd);
    if (oldfd_entry == NULL) {
        return -EBADF;
    }

    if (oldfd == newfd) {
        return -EINVAL;
    }

    fd_entry_t *newfd_entry = posix_fd_entry(newfd);

    if (newfd_entry != NULL) {
        newfd_entry->close(newfd);
    }

    newfd_entry = posix_fd_entry_allocate(newfd);

    if (newfd_entry == NULL) {
        return -ENOMEM;
    }

    *newfd_entry = *oldfd_entry;

    // from dup3 man page:
    // The caller can force the close-on-exec flag to be set for the new file
    // descriptor by specifying O_CLOEXEC in flags.
    if (flags & O_CLOEXEC) {
        newfd_entry->flags |= O_CLOEXEC;
    }

    int err = newfd_entry->dup3(oldfd, newfd);
    if (err) {
        return err;
    }

    return newfd;
}

static long sys_fstat(va_list ap) {
    int fd = va_arg(ap, int);
    struct stat *statbuf = va_arg(ap, struct stat *);

    if (statbuf == NULL) {
        return -EFAULT;
    }

    if (fd == SERVICES_FD) {
        // Return minimal stat for services file
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = S_IFREG | 0444; // regular file, read-only
        statbuf->st_nlink = 1;
        statbuf->st_size = 0;
        return 0;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    return fd_entry->fstat(fd, statbuf);
}

static long sys_fcntl(va_list ap) {
    int fd = va_arg(ap, int);
    int op = va_arg(ap, int);
    int arg = va_arg(ap, int);

    if (fd == SERVICES_FD) {
        return 0;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    switch (op) {
    case F_GETFD: {
        // Only FD_CLOEXEC is supported
        if (fd_entry->flags & O_CLOEXEC) {
            return FD_CLOEXEC;
        } else {
            return 0;
        }
    }
    case F_SETFD: {
        // Only FD_CLOEXEC is supported
        if (arg & FD_CLOEXEC) {
            fd_entry->flags |= O_CLOEXEC;
        } else {
            fd_entry->flags &= ~O_CLOEXEC;
        }
        return 0;
    }
    case F_GETFL: {
        // Return only access mode and file status flags
        return fd_entry->flags & ~(O_CLOEXEC | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
    }
    case F_SETFL: {
        // Only these flags can be changed
        int mask = O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK;
        fd_entry->flags = (fd_entry->flags & ~mask) | (arg & mask);
        return 0;
    }
    }

    return -EINVAL;
}

void libc_init_io() {
    libc_define_syscall(__NR_write, sys_write);
    libc_define_syscall(__NR_read, sys_read);
    libc_define_syscall(__NR_writev, sys_writev);
    libc_define_syscall(__NR_readv, sys_readv);
    libc_define_syscall(__NR_close, sys_close);
    libc_define_syscall(__NR_ioctl, sys_ioctl);
    libc_define_syscall(__NR_dup3, sys_dup3);
    libc_define_syscall(__NR_fstat, sys_fstat);
    libc_define_syscall(__NR_fcntl, sys_fcntl);
}
