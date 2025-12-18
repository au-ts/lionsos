/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>

#include <lions/util.h>

#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>

extern serial_queue_handle_t serial_tx_queue_handle;
extern serial_client_config_t serial_config;

static libc_socket_config_t *socket_config;

static int fd_socket[MAX_FDS];

static ssize_t sock_write(const void *buf, size_t len, int fd) {
    int socket_handle = fd_socket[fd];

    ssize_t wrote = socket_config->tcp_socket_write(socket_handle, buf, len, 0);
    if (wrote == -2) {
        // TODO: error handling at sys_writev etc
        return -EAGAIN;
    } else if (wrote < 0) {
        return -1;
    }

    return wrote;
}

static ssize_t sock_read(void *buf, size_t len, int fd) {
    // TODO: implement
    return 0;
}

static int sock_close(int fd) {
    int socket_handle = fd_socket[fd];
    posix_fd_deallocate(fd);
    fd_socket[fd] = -1;
    return (long)socket_config->tcp_socket_close(socket_handle);
}

// assumes newfd already closed if necessary and refers to a valid fd,
// handled by sys_dup3
static int sock_dup3(int oldfd, int newfd) {
    int socket_handle = fd_socket[oldfd];
    fd_socket[newfd] = socket_handle;
    return socket_config->tcp_socket_dup(socket_handle);
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

    assert(domain == AF_INET);
    assert(type == SOCK_STREAM);
    (void)protocol;

    int socket_handle = socket_config->socket_allocate();
    if (socket_handle != -1) {
        int err = socket_config->tcp_socket_init(socket_handle);
        assert(err == 0);

        int fd = posix_fd_allocate();
        fd_entry_t *fd_entry = posix_fd_entry(fd);

        if (fd_entry != NULL) {
            *fd_entry = (fd_entry_t) {
                .read = sock_read,
                .write = sock_write,
                .close = sock_close,
                .dup3 = sock_dup3,
                .fstat = sock_fstat,
                .flags = O_RDWR,
            };
        } else {
            // TODO: cleanup socket
            return -1;
        }

        fd_socket[fd] = socket_handle;
        return fd;
    } else {
        dlog("POSIX ERROR: sys_socket: could not create socket!\n");
        return -1;
    }
}

static long sys_bind(va_list ap) {
    long fd = va_arg(ap, int);
    const struct sockaddr *sockaddr = va_arg(ap, const struct sockaddr *);
    socklen_t addrlen = va_arg(ap, socklen_t);

    assert(sockaddr->sa_family == AF_INET);
    const struct sockaddr_in *addr_in = (const struct sockaddr_in *)sockaddr;

    uint32_t addr = addr_in->sin_addr.s_addr;
    uint16_t port = ntohs(addr_in->sin_port);

    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ipstr, sizeof(ipstr));

    return (long)socket_config->tcp_socket_bind(fd_socket[fd], addr, port);
}

static long sys_socket_connect(va_list ap) {
    long fd = va_arg(ap, int);
    const struct sockaddr *sockaddr = va_arg(ap, const struct sockaddr *);

    assert(sockaddr->sa_family == AF_INET);
    const struct sockaddr_in *addr_in = (const struct sockaddr_in *)sockaddr;

    uint32_t addr = addr_in->sin_addr.s_addr;
    uint16_t port = ntohs(addr_in->sin_port);

    return (long)socket_config->tcp_socket_connect(fd_socket[fd], addr, port, 0);
}

static long sys_sendto(va_list ap) {
    int sockfd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t len = va_arg(ap, size_t);
    int flags = va_arg(ap, int);
    (void)flags;

    ssize_t wrote = socket_config->tcp_socket_write(fd_socket[sockfd], buf, len, 0);
    if (wrote == -2) {
        return -EAGAIN;
    }

    return (long)wrote;
}

static long sys_recvfrom(va_list ap) {
    int sockfd = va_arg(ap, int);
    void *buf = va_arg(ap, void *);
    size_t len = va_arg(ap, size_t);
    int flags = va_arg(ap, int);
    struct sockaddr *src_addr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    (void)src_addr;
    (void)addrlen;

    int socket_handle = fd_socket[sockfd];
    ssize_t read = socket_config->tcp_socket_recv(socket_handle, buf, len, 0);

    if (read == 0 && flags & MSG_DONTWAIT) {
        return -EAGAIN;
    }
    if (read == -1) {
        return -ENOTCONN;
    }

    return (long)read;
}

static long sys_listen(va_list ap) {
    int sockfd = va_arg(ap, int);
    int backlog = va_arg(ap, int);

    return (long)socket_config->tcp_socket_listen(fd_socket[sockfd], backlog);
}

static long sys_accept(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *sockaddr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    int socket_handle = socket_config->tcp_socket_accept(fd_socket[sockfd], 0);
    assert(socket_handle >= 0);

    int fd = posix_fd_allocate();
    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry != NULL) {
        *fd_entry = (fd_entry_t) {
            .read = sock_read,
            .write = sock_write,
            .close = sock_close,
            .dup3 = sock_dup3,
            .fstat = sock_fstat,
            .flags = O_RDWR,
        };

        fd_socket[fd] = socket_handle;
        if (sockaddr && addrlen) {
            struct sockaddr_in *sin = (struct sockaddr_in *)sockaddr;
            memset(sin, 0, sizeof(*sin));
            sin->sin_family = AF_INET;

            uint32_t addr;
            uint16_t port;
            socket_config->tcp_socket_getpeername(socket_handle, &addr, &port);
            sin->sin_port = htons(port);
            sin->sin_addr.s_addr = addr;
            *addrlen = sizeof(*sin);
        }
    } else {
        // TODO: cleanup socket
        return -1;
    }

    return (long)fd;
}

static long sys_getsockname(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *sockaddr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    if (*addrlen < sizeof(struct sockaddr)) {
        printf("POSIX ERROR: sys_getsockname: addrlen %d too small\n", *addrlen);
        return -EINVAL;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)sockaddr;

    uint32_t addr;
    uint16_t port;

    int err = socket_config->tcp_socket_getsockname(fd_socket[sockfd], &addr, &port);
    assert(err == 0);

    addr_in->sin_family = AF_INET;
    addr_in->sin_addr.s_addr = addr;
    addr_in->sin_port = htons(port);

    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ipstr, sizeof(ipstr));

    return 0;
}

static long sys_getpeername(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *sockaddr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    if (*addrlen < sizeof(struct sockaddr)) {
        printf("POSIX ERROR: sys_getpeername: addrlen %d too small\n", *addrlen);
        return -EINVAL;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)sockaddr;

    uint32_t addr;
    uint16_t port;

    int err = socket_config->tcp_socket_getpeername(fd_socket[sockfd], &addr, &port);
    assert(err == 0);

    addr_in->sin_family = AF_INET;
    addr_in->sin_addr.s_addr = addr;
    addr_in->sin_port = htons(port);

    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ipstr, sizeof(ipstr));

    return 0;
}

void libc_init_sock(libc_socket_config_t *s_cfg) {
    assert(s_cfg != NULL);
    socket_config = s_cfg;
    libc_define_syscall(__NR_socket, sys_socket);
    libc_define_syscall(__NR_bind, sys_bind);
    libc_define_syscall(__NR_connect, sys_socket_connect);
    libc_define_syscall(__NR_setsockopt, sys_setsockopt);
    libc_define_syscall(__NR_getsockopt, sys_getsockopt);
    libc_define_syscall(__NR_sendto, sys_sendto);
    libc_define_syscall(__NR_recvfrom, sys_recvfrom);
    if (s_cfg->tcp_socket_listen) libc_define_syscall(__NR_listen, sys_listen);
    if (s_cfg->tcp_socket_accept) libc_define_syscall(__NR_accept, sys_accept);
    if (s_cfg->tcp_socket_getsockname) libc_define_syscall(__NR_getsockname, sys_getsockname);
    if (s_cfg->tcp_socket_getpeername) libc_define_syscall(__NR_getpeername, sys_getpeername);

    memset(fd_socket, -1, sizeof(fd_socket));
}

int socket_index_of_fd(int fd) {
    int socket_handle = fd_socket[fd];
    assert(socket_handle >= 0);
    return socket_handle;
}
