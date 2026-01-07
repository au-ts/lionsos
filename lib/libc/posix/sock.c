/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
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

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    return socket_config->tcp_socket_write(socket_handle, buf, len, fd_entry->flags);
}

static ssize_t sock_read(void *buf, size_t len, int fd) {
    int socket_handle = fd_socket[fd];

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    return socket_config->tcp_socket_recv(socket_handle, buf, len, fd_entry->flags);
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

static long sys_setsockopt(va_list ap) {
    int sockfd = va_arg(ap, int);
    int level = va_arg(ap, int);
    int optname = va_arg(ap, int);
    const void *optval = va_arg(ap, const void *);
    socklen_t optlen = va_arg(ap, socklen_t);

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (level == SOL_SOCKET && optname == SO_LINGER) {
        if (optval == NULL || optlen < sizeof(struct linger)) {
            return -EINVAL;
        }

        // Accept but ignore linger option
        return 0;
    }

    return -ENOPROTOOPT;
}

static long sys_getsockopt(va_list ap) {
    int sockfd = va_arg(ap, int);
    int level = va_arg(ap, int);
    int optname = va_arg(ap, int);
    void *optval = va_arg(ap, void *);
    socklen_t *optlen = va_arg(ap, socklen_t *);

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);

    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[sockfd] == -1) {
        return -ENOTSOCK;
    }

    if (level == SOL_SOCKET && optname == SO_ERROR) {
        if (optlen == NULL || optval == NULL) {
            return -EFAULT;
        }
        if (*optlen < sizeof(int)) {
            return -EINVAL;
        }

        int socket_handle = fd_socket[sockfd];
        int err = 0;
        if (socket_config->tcp_socket_err) {
            err = socket_config->tcp_socket_err(socket_handle);
        }

        *(int *)optval = err;
        *optlen = sizeof(int);
        return 0;
    }

    return -ENOPROTOOPT;
}

static long sys_socket(va_list ap) {
    int domain = va_arg(ap, int);
    int type = va_arg(ap, int);
    int protocol = va_arg(ap, int);

    (void)protocol;

    if (domain != AF_INET) {
        return -EAFNOSUPPORT;
    }

    if (type != SOCK_STREAM) {
        return -ESOCKTNOSUPPORT;
    }

    int fd = posix_fd_allocate();
    if (fd < 0) {
        return -EMFILE;
    }
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
        // Unreachable: we just allocated fd.
        assert(false);
        return -EBADF;
    }

    int socket_handle = socket_config->socket_allocate();
    if (socket_handle < 0) {
        posix_fd_deallocate(fd);
        return socket_handle;
    }

    int err = socket_config->tcp_socket_init(socket_handle);
    if (err < 0) {
        socket_config->tcp_socket_close(socket_handle);
        posix_fd_deallocate(fd);
        return err;
    };

    fd_socket[fd] = socket_handle;
    return fd;
}

static long sys_bind(va_list ap) {
    long fd = va_arg(ap, int);
    const struct sockaddr *sockaddr = va_arg(ap, const struct sockaddr *);
    socklen_t addrlen = va_arg(ap, socklen_t);

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[fd] == -1) {
        return -ENOTSOCK;
    }

    if (sockaddr == NULL) {
        return -EFAULT;
    }

    if (sockaddr->sa_family != AF_INET) {
        return -EAFNOSUPPORT;
    }

    if (addrlen < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    const struct sockaddr_in *addr_in = (const struct sockaddr_in *)sockaddr;

    uint32_t addr = addr_in->sin_addr.s_addr;
    uint16_t port = ntohs(addr_in->sin_port);

    return (long)socket_config->tcp_socket_bind(fd_socket[fd], addr, port);
}

static long sys_socket_connect(va_list ap) {
    long fd = va_arg(ap, int);
    const struct sockaddr *sockaddr = va_arg(ap, const struct sockaddr *);
    socklen_t addrlen = va_arg(ap, socklen_t);

    fd_entry_t *fd_entry = posix_fd_entry(fd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[fd] == -1) {
        return -ENOTSOCK;
    }

    if (sockaddr == NULL) {
        return -EFAULT;
    }

    if (sockaddr->sa_family != AF_INET) {
        return -EAFNOSUPPORT;
    }

    if (addrlen < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    const struct sockaddr_in *addr_in = (const struct sockaddr_in *)sockaddr;

    uint32_t addr = addr_in->sin_addr.s_addr;
    uint16_t port = ntohs(addr_in->sin_port);

    return (long)socket_config->tcp_socket_connect(fd_socket[fd], addr, port, fd_entry->flags);
}

static long sys_sendto(va_list ap) {
    int sockfd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t len = va_arg(ap, size_t);
    int flags = va_arg(ap, int);

    if (buf == NULL) {
        return -EFAULT;
    }

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[sockfd] == -1) {
        return -ENOTSOCK;
    }

    int effective_flags = fd_entry->flags;
    if (flags & MSG_DONTWAIT) {
        effective_flags |= O_NONBLOCK;
    }
    return socket_config->tcp_socket_write(fd_socket[sockfd], buf, len, effective_flags);
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

    if (buf == NULL) {
        return -EFAULT;
    }

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[sockfd] == -1) {
        return -ENOTSOCK;
    }

    int effective_flags = fd_entry->flags;
    if (flags & MSG_DONTWAIT) {
        effective_flags |= O_NONBLOCK;
    }
    return socket_config->tcp_socket_recv(fd_socket[sockfd], buf, len, effective_flags);
}

static long sys_listen(va_list ap) {
    int sockfd = va_arg(ap, int);
    int backlog = va_arg(ap, int);

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[sockfd] == -1) {
        return -ENOTSOCK;
    }

    return (long)socket_config->tcp_socket_listen(fd_socket[sockfd], backlog);
}

static long sys_accept(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *sockaddr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[sockfd] == -1) {
        return -ENOTSOCK;
    }

    int socket_handle = socket_config->tcp_socket_accept(fd_socket[sockfd], fd_entry->flags);

    if (socket_handle < 0) {
        return socket_handle;
    }

    int fd = posix_fd_allocate();
    if (fd < 0) {
        socket_config->tcp_socket_close(socket_handle);
        return -EMFILE;
    }

    fd_entry = posix_fd_entry(fd);
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
        // Unreachable: we just allocated fd.
        assert(false);
        return -EBADF;
    }

    return (long)fd;
}

static long sys_getsockname(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *sockaddr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[sockfd] == -1) {
        return -ENOTSOCK;
    }

    if (addrlen == NULL || sockaddr == NULL) {
        return -EFAULT;
    }

    if (*addrlen < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)sockaddr;

    uint32_t addr;
    uint16_t port;

    int err = socket_config->tcp_socket_getsockname(fd_socket[sockfd], &addr, &port);
    if (err != 0) {
        return err;
    }

    addr_in->sin_family = AF_INET;
    addr_in->sin_addr.s_addr = addr;
    addr_in->sin_port = htons(port);

    *addrlen = sizeof(struct sockaddr_in);

    return 0;
}

static long sys_getpeername(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *sockaddr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    fd_entry_t *fd_entry = posix_fd_entry(sockfd);
    if (fd_entry == NULL) {
        return -EBADF;
    }

    if (fd_socket[sockfd] == -1) {
        return -ENOTSOCK;
    }

    if (addrlen == NULL || sockaddr == NULL) {
        return -EFAULT;
    }

    if (*addrlen < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)sockaddr;

    uint32_t addr;
    uint16_t port;

    int err = socket_config->tcp_socket_getpeername(fd_socket[sockfd], &addr, &port);
    if (err != 0) {
        return err;
    }

    addr_in->sin_family = AF_INET;
    addr_in->sin_addr.s_addr = addr;
    addr_in->sin_port = htons(port);

    *addrlen = sizeof(struct sockaddr_in);

    return 0;
}

static long sys_ppoll(va_list ap) {
    struct pollfd *fds = va_arg(ap, struct pollfd *);
    nfds_t nfds = va_arg(ap, nfds_t);
    const struct timespec *timeout_ts = va_arg(ap, const struct timespec *);
    const sigset_t *sigmask = va_arg(ap, const sigset_t *);

    // Ignore timeout and sigmask for now
    (void)timeout_ts;
    (void)sigmask;

    if (fds == NULL && nfds > 0) {
        return -EFAULT;
    }

    if (nfds > MAX_FDS) {
        return -EINVAL;
    }

    int ready = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        int fd = fds[i].fd;
        fds[i].revents = 0;

        if (fd < 0) {
            continue;
        }

        fd_entry_t *fd_entry = posix_fd_entry(fd);
        if (fd_entry == NULL) {
            fds[i].revents = POLLNVAL;
            ready++;
            continue;
        }

        if (fd_socket[fd] != -1) {
            int sock = fd_socket[fd];
            if ((fds[i].events & POLLIN) && socket_config->tcp_socket_readable(sock)) {
                fds[i].revents |= POLLIN;
            }
            if ((fds[i].events & POLLOUT) && socket_config->tcp_socket_writable(sock)) {
                fds[i].revents |= POLLOUT;
            }

            // POLLHUP, POLLERR always checked regardless of events
            if (socket_config->tcp_socket_hup(sock)) {
                fds[i].revents |= POLLHUP;
            }
            if (socket_config->tcp_socket_err(sock) != 0) {
                fds[i].revents |= POLLERR;
            }
            if (fds[i].revents) {
                ready++;
            }
        } else {
            // Regular files are always ready
            if (fds[i].events & POLLIN) {
                fds[i].revents |= POLLIN;
            }
            if (fds[i].events & POLLOUT) {
                fds[i].revents |= POLLOUT;
            }
            if (fds[i].revents) {
                ready++;
            }
        }
    }
    return ready;
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
    if (s_cfg->tcp_socket_listen) {
        libc_define_syscall(__NR_listen, sys_listen);
    }
    if (s_cfg->tcp_socket_accept) {
        libc_define_syscall(__NR_accept, sys_accept);
    }
    if (s_cfg->tcp_socket_getsockname) {
        libc_define_syscall(__NR_getsockname, sys_getsockname);
    }
    if (s_cfg->tcp_socket_getpeername) {
        libc_define_syscall(__NR_getpeername, sys_getpeername);
    }
    if (s_cfg->tcp_socket_readable && s_cfg->tcp_socket_writable && s_cfg->tcp_socket_hup && s_cfg->tcp_socket_err) {
        libc_define_syscall(__NR_ppoll, sys_ppoll);
    }

    memset(fd_socket, -1, sizeof(fd_socket));
}

int socket_index_of_fd(int fd) {
    int socket_handle = fd_socket[fd];
    assert(socket_handle >= 0);
    return socket_handle;
}
