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
#include <sys/uio.h>

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>

long sys_write(va_list ap) {
    int fd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t count = va_arg(ap, size_t);

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    return fd_entry->write(buf, count, fd);
}

long sys_read(va_list ap) {
    int fd = va_arg(ap, int);
    void *buf = va_arg(ap, void *);
    size_t count = va_arg(ap, size_t);

    if (fd == SERVICES_FD) {
        // Just return EOF for now
        return 0;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    return fd_entry->read(buf, count, fd);
}

long sys_writev(va_list ap) {
    int fd = va_arg(ap, int);
    struct iovec *iov = va_arg(ap, struct iovec *);
    int iovcnt = va_arg(ap, int);

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
        fd_entry_t *fd_entry = posix_fd_entry(fd);
        size_t written = fd_entry->write(iov[i].iov_base, iov[i].iov_len, fd);

        // TODO: check written

        ret += written;
    }

    return ret;
}

long sys_readv(va_list ap) {
    int fd = va_arg(ap, int);
    const struct iovec *iov = va_arg(ap, const struct iovec *);
    int iovcnt = va_arg(ap, int);

    ssize_t ret = 0;
    for (int i = 0; i < iovcnt; i++) {
        fd_entry_t *fd_entry = posix_fd_entry(fd);
        size_t read = fd_entry->read(iov[i].iov_base, iov[i].iov_len, fd);

        // TODO: check read

        ret += read;
    }

    return ret;
}

long sys_close(va_list ap) {
    long fd = va_arg(ap, int);

    if (fd == SERVICES_FD) {
        return 0;
    }

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -1;
    }

    int err = fd_entry->close(fd);
    if (err) {
        return -1;
    }

    err = posix_fd_deallocate(fd);
    if (err) {
        return -1;
    }

    return 0;
}

long sys_ioctl(va_list ap) {
    int fd = va_arg(ap, int);
    int request = va_arg(ap, int);
    (void)request;
    // dlog("musl called ioctl on fd %d", fd);
    /* muslc does some ioctls to stdout, so just allow these to silently
       go through */
    if (fd == STDOUT_FD) {
        return 0;
    }

    return 0;
}

static long sys_dup3(va_list ap) {
    int oldfd = va_arg(ap, int);
    int newfd = va_arg(ap, int);
    int flags = va_arg(ap, int);

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

    *newfd_entry = *oldfd_entry;

    // from dup3 man page:
    // The caller can force the close-on-exec flag to be set for the new file
    // descriptor by specifying O_CLOEXEC in flags.
    if (flags & O_CLOEXEC) {
        newfd_entry->flags |= O_CLOEXEC;
    }

    int err = newfd_entry->dup3(oldfd, newfd);
    if (err) {
        return -1;
    }

    return newfd;
}

static long sys_fstat(va_list ap) {
    int fd = va_arg(ap, int);
    struct stat *statbuf = va_arg(ap, struct stat *);

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    return fd_entry->fstat(fd, statbuf);
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
}
