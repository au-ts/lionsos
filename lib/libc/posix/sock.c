/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>
#include <lions/posix/tcp.h>
#include <lions/util.h>

static int fd_socket[MAX_FDS];
static int socket_refcount[MAX_SOCKETS];

static size_t sock_write(const void *buf, size_t len, int fd) {
    int socket_handle = fd_socket[fd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    int wrote = tcp_socket_write(socket_handle, buf, len);
    if (wrote == -2) {
        // TODO: error handling at sys_writev etc
        return -EAGAIN;
    } else if (wrote < 0) {
        return -1;
    }

    return wrote;
}

static size_t sock_read(void *buf, size_t len, int fd) {
    // TODO: implement
    return 0;
}

static int sock_close(int fd) {
    int socket_handle = fd_socket[fd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    fd_socket[fd] = 0;

    socket_refcount[socket_handle]--;
    if (socket_refcount[socket_handle] == 0) {
        return (long)tcp_socket_close(socket_handle);
    }

    return 0;
}

// assumes newfd already closed if necessary and refers to a valid fd,
// handled by sys_dup3
static int sock_dup3(int oldfd, int newfd) {
    int oldfd_socket_handle = fd_socket[oldfd];

    assert(oldfd_socket_handle >= 0);
    assert(oldfd_socket_handle < MAX_SOCKETS);
    assert(socket_refcount[oldfd_socket_handle] != 0);

    fd_socket[newfd] = oldfd_socket_handle;
    socket_refcount[oldfd_socket_handle]++;

    return 0;
}

static int sock_fstat(int fd, struct stat *statbuf) {
    statbuf->st_mode = S_IFSOCK | 0777;
    return 0;
}

static long sys_setsockopt(va_list ap) { return 0; }

static long sys_getsockopt(va_list ap) { return 0; }

static long sys_socket(va_list ap) {
    int domain = va_arg(ap, int);
    int type = va_arg(ap, int);
    int protocol = va_arg(ap, int);

    (void)domain;
    (void)type;
    (void)protocol;

    int socket_handle = tcp_socket_create();
    if (socket_handle != -1) {
        socket_refcount[socket_handle]++;
        int fd = posix_fd_allocate();
        fd_entry_t *fd_entry = posix_fd_entry(fd);

        if (fd_entry != NULL) {
            *fd_entry = (fd_entry_t){
                .read = sock_read,
                .write = sock_write,
                .close = sock_close,
                .dup3 = sock_dup3,
                .fstat = sock_fstat,
            };
        } else {
            // TODO: cleanup socket
            return -1;
        }

        fd_socket[fd] = socket_handle;
        return fd;
    } else {
        dlog("sys_socket could not create socket!\n");
        return -1;
    }
}

static long sys_bind(va_list ap) { return 0; }

static long sys_socket_connect(va_list ap) {
    long fd = va_arg(ap, int);
    const struct sockaddr *sockaddr = va_arg(ap, const struct sockaddr *);

    int socket_handle = fd_socket[fd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    uint16_t port = sockaddr->sa_data[0] << 8 | sockaddr->sa_data[1];
    uint32_t addr =
        sockaddr->sa_data[2] | sockaddr->sa_data[3] << 8 | sockaddr->sa_data[4] << 16 | sockaddr->sa_data[5] << 24;

    return (long)tcp_socket_connect(socket_handle, port, addr);
}

static long sys_sendto(va_list ap) {
    int sockfd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t len = va_arg(ap, size_t);
    int flags = va_arg(ap, int);
    (void)flags;

    int socket_handle = fd_socket[sockfd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    int wrote = tcp_socket_write(socket_handle, buf, len);
    if (wrote == -2) {
        return -EAGAIN;
    }

    return (long)wrote;
}

static long sys_recvfrom(va_list ap) {
    int sockfd = va_arg(ap, int);
    void *buf = va_arg(ap, void *);
    ssize_t len = va_arg(ap, int);
    int flags = va_arg(ap, int);
    struct sockaddr *src_addr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    (void)src_addr;
    (void)addrlen;

    int socket_handle = fd_socket[sockfd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    int read = tcp_socket_recv(socket_handle, buf, len);

    if (read == 0 && flags & MSG_DONTWAIT) {
        return -EAGAIN;
    }
    if (read == -1) {
        return -ENOTCONN;
    }

    return (long)read;
}

void libc_init_sock() {
    libc_define_syscall(__NR_socket, sys_socket);
    libc_define_syscall(__NR_bind, sys_bind);
    libc_define_syscall(__NR_connect, sys_socket_connect);
    libc_define_syscall(__NR_setsockopt, sys_setsockopt);
    libc_define_syscall(__NR_getsockopt, sys_getsockopt);
    libc_define_syscall(__NR_sendto, sys_sendto);
    libc_define_syscall(__NR_recvfrom, sys_recvfrom);
}

int socket_index_of_fd(int fd) { return fd_socket[fd]; }
