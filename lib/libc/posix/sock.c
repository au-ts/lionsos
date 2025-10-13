#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>

#include <lions/posix/posix.h>
#include <lions/posix/tcp.h>
#include <lions/util.h>

#define MAX_SOCKET_FDS 100
#define LWIP_FD_START 3

static int fd_socket[MAX_SOCKET_FDS];
static int fd_active[MAX_SOCKET_FDS];
static int socket_refcount[MAX_SOCKETS];

static long sys_setsockopt(va_list ap) { return 0; }

static long sys_getsockopt(va_list ap) { return 0; }

static long sys_socket(va_list ap) {
    long fd = -1;
    for (int i = LWIP_FD_START; i < MAX_SOCKET_FDS; i++) {
        if (!fd_active[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        dlog("couldn't find available fd");
        return -1;
    }

    int socket_handle = tcp_socket_create();
    if (socket_handle != -1) {
        socket_refcount[socket_handle]++;
        fd_active[fd] = true;
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

    assert(fd_active[fd]);

    int socket_handle = fd_socket[fd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    const struct sockaddr *sockaddr = va_arg(ap, const struct sockaddr *);

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

    assert(fd_active[sockfd]);

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
    (void)src_addr;
    socklen_t *addrlen = va_arg(ap, socklen_t *);
    (void)addrlen;

    assert(fd_active[sockfd]);

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

//FIXME: needs generic file/socket implementation
static long sys_dup3(va_list ap) {
    int oldfd = va_arg(ap, int);
    int newfd = va_arg(ap, int);
    int flags = va_arg(ap, int);
    (void)flags;

    assert(fd_active[oldfd]);
    int oldfd_socket_handle = fd_socket[oldfd];

    assert(oldfd_socket_handle >= 0);
    assert(oldfd_socket_handle < MAX_SOCKETS);
    assert(socket_refcount[oldfd_socket_handle] != 0);

    if (fd_active[newfd]) {
        int newfd_socket_handle = fd_socket[newfd];
        socket_refcount[newfd_socket_handle]--;
        if (socket_refcount[newfd_socket_handle] == 0) {
            tcp_socket_close(newfd_socket_handle);
        }
    }

    fd_active[newfd] = true;
    fd_socket[newfd] = oldfd_socket_handle;

    socket_refcount[oldfd_socket_handle]++;

    return newfd;
}

void libc_init_sock() {
    libc_define_syscall(__NR_socket, (muslcsys_syscall_t)sys_socket);
    libc_define_syscall(__NR_bind, (muslcsys_syscall_t)sys_bind);
    libc_define_syscall(__NR_connect, (muslcsys_syscall_t)sys_socket_connect);
    libc_define_syscall(__NR_setsockopt, (muslcsys_syscall_t)sys_setsockopt);
    libc_define_syscall(__NR_getsockopt, (muslcsys_syscall_t)sys_getsockopt);
    libc_define_syscall(__NR_sendto, (muslcsys_syscall_t)sys_sendto);
    libc_define_syscall(__NR_recvfrom, (muslcsys_syscall_t)sys_recvfrom);
    libc_define_syscall(__NR_dup3, (muslcsys_syscall_t)sys_dup3);
}

int socket_index_of_fd(int fd) {
    assert(fd_active[fd]);
    return fd_socket[fd];
}
