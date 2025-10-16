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

static int socket_int() {
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
        printf("socket created with fd %d and socket handle %d\n", fd, socket_handle);
        return fd;
    } else {
        dlog("socket_int could not create socket!\n");
        return -1;
    }
}

static long sys_socket(va_list ap) {
    int domain = va_arg(ap, int);
    int type = va_arg(ap, int);
    int protocol = va_arg(ap, int);

    (void)domain;
    (void)type;
    (void)protocol;

    return (long)socket_int();
}

static uint16_t socket_port[MAX_FDS];
static uint32_t socket_addr[MAX_FDS];

static long sys_bind(va_list ap) {
    long fd = va_arg(ap, int);
    const struct sockaddr *addr = va_arg(ap, const struct sockaddr *);
    socklen_t addrlen = va_arg(ap, socklen_t);

    uint16_t port = addr->sa_data[0] << 8 | addr->sa_data[1];
    uint32_t bind_addr =
        addr->sa_data[2] | addr->sa_data[3] << 8 | addr->sa_data[4] << 16 | addr->sa_data[5] << 24;

    printf("Binding socket fd %d to port %d addr %d.%d.%d.%d\n", fd, port, addr->sa_data[2],
           addr->sa_data[3], addr->sa_data[4], addr->sa_data[5]);

    socket_port[fd] = port;
    socket_addr[fd] = bind_addr;

    return 0;
}

static long sys_socket_connect(va_list ap) {
    long fd = va_arg(ap, int);
    const struct sockaddr *sockaddr = va_arg(ap, const struct sockaddr *);

    printf("Connecting socket fd %d to port %d addr %d.%d.%d.%d\n", fd,
           sockaddr->sa_data[0] << 8 | sockaddr->sa_data[1],
           sockaddr->sa_data[2], sockaddr->sa_data[3], sockaddr->sa_data[4], sockaddr->sa_data[5]);

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

static long sys_listen(va_list ap) { return 0; }

static long sys_accept(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *addr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    printf("sys_accept called on sockfd %d with addr %p and addrlen %p\n", sockfd, addr, addrlen);
    return (long)socket_int();
}

static long sys_getsockname(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *addr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    printf("sys_getsockname called on sockfd %d with addrlen %d\n", sockfd, *addrlen);

    if (*addrlen < sizeof(struct sockaddr)) {
        printf("sys_getsockname: addrlen %d too small\n", *addrlen);
        return -EINVAL;
    }

    addr->sa_family = AF_INET;
    addr->sa_data[0] = (socket_port[sockfd] >> 8) & 0xFF;
    addr->sa_data[1] = (socket_port[sockfd] >> 0) & 0xFF;
    addr->sa_data[2] = (socket_addr[sockfd] >> 0) & 0xFF;
    addr->sa_data[3] = (socket_addr[sockfd] >> 8) & 0xFF;
    addr->sa_data[4] = (socket_addr[sockfd] >> 16) & 0xFF;
    addr->sa_data[5] = (socket_addr[sockfd] >> 24) & 0xFF;

    return 0;
}

static long sys_getpeername(va_list ap) {
    int sockfd = va_arg(ap, int);
    struct sockaddr *addr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

    printf("sys_getpeername called on sockfd %d with addrlen %d\n", sockfd, *addrlen);


    if (*addrlen < sizeof(struct sockaddr)) {
        printf("sys_getpeername: addrlen %d too small\n", *addrlen);
        return -EINVAL;
    }

    addr->sa_family = AF_INET;
    addr->sa_data[0] = 0; // port high byte
    addr->sa_data[1] = 0; // port low byte
    addr->sa_data[2] = 127; // addr byte 0
    addr->sa_data[3] = 0; // addr byte 1
    addr->sa_data[4] = 0; // addr byte 2
    addr->sa_data[5] = 1; // addr byte 3

    return 0;
}

void libc_init_sock() {
    libc_define_syscall(__NR_socket, sys_socket);
    libc_define_syscall(__NR_bind, sys_bind);
    libc_define_syscall(__NR_connect, sys_socket_connect);
    libc_define_syscall(__NR_setsockopt, sys_setsockopt);
    libc_define_syscall(__NR_getsockopt, sys_getsockopt);
    libc_define_syscall(__NR_sendto, sys_sendto);
    libc_define_syscall(__NR_recvfrom, sys_recvfrom);
    libc_define_syscall(__NR_listen, sys_listen);
    libc_define_syscall(__NR_accept, sys_accept);
    libc_define_syscall(__NR_getsockname, sys_getsockname);
    libc_define_syscall(__NR_getpeername, sys_getpeername);
}

int socket_index_of_fd(int fd) { return fd_socket[fd]; }
