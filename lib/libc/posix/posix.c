/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <stdarg.h>
#include <bits/syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <autoconf.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>

#include <lions/posix/posix.h>
#include <lions/posix/tcp.h>
#include <lions/util.h>

#include <lions/fs/helpers.h>
#include <lions/fs/config.h>

#define MUSLC_HIGHEST_SYSCALL SYS_pkey_free
#define MUSLC_NUM_SYSCALLS (MUSLC_HIGHEST_SYSCALL + 1)
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON

#define STDOUT_FD 1
#define STDERR_FD 2
#define LWIP_FD_START 3

typedef long (*muslcsys_syscall_t)(va_list);

extern void *__sysinfo;

extern serial_queue_handle_t serial_tx_queue_handle;

extern serial_client_config_t serial_config;

static muslcsys_syscall_t syscall_table[MUSLC_NUM_SYSCALLS] = {0};

long sel4_vsyscall(long sysnum, ...);

/*
 * Statically allocated morecore area.
 *
 * This is rather terrible, but is the simplest option without a
 * huge amount of infrastructure.
 */
#define MORECORE_AREA_BYTE_SIZE 0x100000
char morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Pointer to free space in the morecore area. */
static uintptr_t morecore_base = (uintptr_t)&morecore_area;
static uintptr_t morecore_top = (uintptr_t)&morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Actual morecore implementation
   returns 0 if failure, returns newbrk if success.
*/

int fd_socket[MAX_SOCKET_FDS];
int fd_active[MAX_SOCKET_FDS];
int socket_refcount[MAX_SOCKETS];

int fd_flags[MAX_SOCKET_FDS];

char fd_path[MAX_SOCKET_FDS][100];

static size_t output(void *data, size_t count) {
    char *src = data;
    uint32_t sent = 0;
    while (sent < count) {
        char *nl = memchr(src, '\n', count - sent);
        uint32_t stop = (nl == NULL) ? count - sent : nl - src;
        /* Enqueue to the first '\n' character, end of string
           or until queue is full */
        uint32_t enq = serial_enqueue_batch(&serial_tx_queue_handle, stop, src);
        sent += enq;
        if (sent == count || serial_queue_free(&serial_tx_queue_handle) < 2) {
            break;
        }

        /* Append a '\r' character before a '\n' character */
        serial_enqueue_batch(&serial_tx_queue_handle, 2, "\r\n");
        sent += 1;
        src += enq + 1;
    }

    if (sent) {
        microkit_notify(serial_config.tx.id);
    }

    return sent;
}

long sys_brk(va_list ap) {
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    /* if the newbrk is 0, return the bottom of the heap */
    if (!newbrk) {
        return morecore_base;
    } else if (newbrk < morecore_top && newbrk > (uintptr_t)&morecore_area[0]) {
        return morecore_base = newbrk;
    }
    return 0;
}

uintptr_t align_addr(uintptr_t addr) { return (addr + 0xfff) & ~0xfff; }

long sys_mmap(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    (void)fd, (void)offset, (void)prot, (void)addr;

    if (flags & MAP_ANONYMOUS) {
        /* Check that we don't try and allocate more than exists */
        if (length > morecore_top - morecore_base) {
            return -ENOMEM;
        }
        /* Steal from the top */
        morecore_top -= length;
        return morecore_top;
    }
    return -ENOMEM;
}

long sys_munmap(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t len = va_arg(ap, size_t);
    (void)addr, (void)len;

    return 0;
}

long sys_mprotect(va_list ap) {
    void *addr = va_arg(ap, void *);
    size_t size = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    (void)addr, (void)size, (void)prot;

    return 0;
}

long sys_madvise(va_list ap) { return 0; }

long sys_write(va_list ap) {
    int fd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t count = va_arg(ap, size_t);

    if (fd == 1 || fd == 2) {
        uint32_t n = serial_enqueue_batch(&serial_tx_queue_handle, count, buf);

        if (n) {
            microkit_notify(serial_config.tx.id);
        }

        return n;
    }

    return -1;
}

long sys_read(va_list ap) {
    int fd = va_arg(ap, int);
    assert(fd == SERVICES_FD);
    return 0;
}

long sys_readv(va_list ap) {
    int fd = va_arg(ap, int);
    const struct iovec *iov = va_arg(ap, const struct iovec *);
    int iovcnt = va_arg(ap, int);
    ssize_t ret = 0;

    for (int i = 0; i < iovcnt; i++) {
        ptrdiff_t read_buffer;
        fs_buffer_allocate(&read_buffer);

        fs_cmpl_t completion;
        fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_READ,
                                                    .params.file_read = {
                                                        .fd = fd,
                                                        .offset = ret,
                                                        .buf.offset = read_buffer,
                                                        .buf.size = iov[i].iov_len,
                                                    }});

        if (completion.status != FS_STATUS_SUCCESS) {
            fs_buffer_free(read_buffer);
            return -completion.status;
        }
        int read = completion.data.file_read.len_read;
        memcpy(iov[i].iov_base, fs_buffer_ptr(read_buffer), read);
        fs_buffer_free(read_buffer);
        ret += read;
    }

    return ret;
}

long sys_clock_gettime(va_list ap) {
    clockid_t clk_id = va_arg(ap, clockid_t);
    (void)clk_id;
    struct timespec *tp = va_arg(ap, struct timespec *);

    uint64_t rtc = 0;

    tp->tv_sec = rtc / 1000;
    tp->tv_nsec = (rtc % 1000) * 1000000;

    return 0;
}

long sys_getpid(va_list ap) { return 0; }

long sys_ioctl(va_list ap) {
    int fd = va_arg(ap, int);
    int request = va_arg(ap, int);
    (void)request;
    dlog("musl called ioctl on fd %d", fd);
    /* muslc does some ioctls to stdout, so just allow these to silently
       go through */
    if (fd == STDOUT_FD) {
        return 0;
    }

    return 0;
}

long sys_writev(va_list ap) {
    int fildes = va_arg(ap, int);
    struct iovec *iov = va_arg(ap, struct iovec *);
    int iovcnt = va_arg(ap, int);

    long long sum = 0;
    ssize_t ret = 0;

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

    int socket_handle = fd_socket[fildes];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);

    /* Write the buffer to console if the fd is for stdout or stderr. */
    if (fildes == STDOUT_FD || fildes == STDERR_FD) {
        for (int i = 0; i < iovcnt; i++) {
            ret += output(iov[i].iov_base, iov[i].iov_len);
        }
    } else if (socket_refcount[socket_handle] == 0) {
        for (int i = 0; i < iovcnt; i++) {
            ptrdiff_t write_buffer;
            int err = fs_buffer_allocate(&write_buffer);

            memcpy(fs_buffer_ptr(write_buffer), iov[i].iov_base, iov[i].iov_len);

            fs_cmpl_t completion;
            err = fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_WRITE,
                                                              .params.file_write = {
                                                                  .fd = fildes,
                                                                  .offset = ret,
                                                                  .buf.offset = write_buffer,
                                                                  .buf.size = iov[i].iov_len,
                                                              }});
            fs_buffer_free(write_buffer);

            if (completion.status != FS_STATUS_SUCCESS) {
                return -completion.status;
            }

            int wrote = completion.data.file_write.len_written;
            if (wrote < 0) {
                if (ret == 0) {
                    if (wrote == -2) {
                        ret = -EAGAIN;
                    } else {
                        ret = -1;
                    }
                }
                return ret;
            }
            ret += wrote;
        }
    }
    return ret;
}

long sys_openat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *path = va_arg(ap, const char *);
    int flags = va_arg(ap, int);

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
        return -completion.status;
    }

    uint64_t fd;
    if (flags & O_DIRECTORY) {
        fd = completion.data.dir_open.fd;
    } else {
        fd = completion.data.file_open.fd;
    }

    fd_active[fd] = true;
    fd_flags[fd] = flags;

    strcpy(fd_path[fd], path);
    return fd;
}

long sys_getuid(va_list ap) {
    (void)ap;
    return 501;
}

long sys_getgid(va_list ap) {
    (void)ap;
    return 501;
}

long sys_fcntl(va_list ap) {
    int fd = va_arg(ap, int);
    int op = va_arg(ap, int);

    switch (op) {
        case F_GETFL:
            return fd_flags[fd];
            break;
        default:
    }

    return 0;
}

long sys_setsockopt(va_list ap) { return 0; }

long sys_getsockopt(va_list ap) { return 0; }

long sys_socket(va_list ap) {
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

long sys_bind(va_list ap) { return 0; }

long sys_socket_connect(va_list ap) {
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

long sys_close(va_list ap) {
    long fd = va_arg(ap, int);

    if (fd == SERVICES_FD) {
        return 0;
    }

    assert(fd_active[fd]);

    int socket_handle = fd_socket[fd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);

    if (socket_refcount[socket_handle] != 0) {
        fd_socket[fd] = 0;
        fd_active[fd] = 0;

        socket_refcount[socket_handle]--;
        if (socket_refcount[socket_handle] == 0) {
            return (long)tcp_socket_close(socket_handle);
        }
    } else {
        fs_cmpl_t completion;
        if (fd_flags[fd] & O_DIRECTORY) {
            fs_command_blocking(&completion, (fs_cmd_t){
                                                 .type = FS_CMD_DIR_CLOSE,
                                                 .params.dir_close.fd = fd,
                                             });
        } else {
            fs_command_blocking(&completion, (fs_cmd_t){
                                                 .type = FS_CMD_FILE_CLOSE,
                                                 .params.file_close.fd = fd,
                                             });
        }
        assert(completion.status == FS_STATUS_SUCCESS);
        fd_active[fd] = false;
    }

    return 0;
}

long sys_dup3(va_list ap) {
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

long sys_sendto(va_list ap) {
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

long sys_recvfrom(va_list ap) {
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
    fs_buffer_free(output_buffer);

    return 0;
}

long sys_fstat(va_list ap) {
    int fd = va_arg(ap, int);
    struct stat *statbuf = va_arg(ap, struct stat *);

    char *path = fd_path[fd];

    return fstat_int(path, statbuf);
}

long sys_fstatat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *path = va_arg(ap, const char *);
    struct stat *statbuf = va_arg(ap, struct stat *);

    (void)dirfd;

    return fstat_int(path, statbuf);
}

long sys_readlinkat(va_list ap) { return -EINVAL; }

long sys_getrandom(va_list ap) {
    void *buf = va_arg(ap, void *);
    size_t buflen = va_arg(ap, size_t);
    unsigned flags = va_arg(ap, unsigned);

    size_t bytes_written = 0;

    while (bytes_written < buflen) {
        int r = rand();

        size_t bytes_to_copy = sizeof(int);
        if (bytes_written + bytes_to_copy > buflen) {
            bytes_to_copy = buflen - bytes_written;
        }

        memcpy(buf + bytes_written, &r, bytes_to_copy);
        bytes_written += bytes_to_copy;
    }

    return bytes_written;
}

void debug_error(long num) { dlog("error doing syscall: %d", num); }

int pthread_setcancelstate(int state, int *oldstate) {
    (void)state;
    (void)oldstate;
    return 0;
}

long sel4_vsyscall(long sysnum, ...) {
    va_list al;
    va_start(al, sysnum);
    muslcsys_syscall_t syscall;

    if (sysnum < 0 || sysnum >= ARRAY_SIZE(syscall_table)) {
        debug_error(sysnum);
        return -ENOSYS;
    } else {
        syscall = syscall_table[sysnum];
    }
    /* Check a syscall is implemented there */
    if (!syscall) {
        debug_error(sysnum);
        return -ENOSYS;
    }
    /* Call it */
    long ret = syscall(al);
    va_end(al);
    return ret;
}

void libc_init() {
    /* Syscall table init */
    __sysinfo = sel4_vsyscall;
    syscall_table[__NR_brk] = (muslcsys_syscall_t)sys_brk;
    syscall_table[__NR_write] = (muslcsys_syscall_t)sys_write;
    syscall_table[__NR_mmap] = (muslcsys_syscall_t)sys_mmap;
    syscall_table[__NR_munmap] = (muslcsys_syscall_t)sys_munmap;
    syscall_table[__NR_mprotect] = (muslcsys_syscall_t)sys_mprotect;
    syscall_table[__NR_getpid] = (muslcsys_syscall_t)sys_getpid;
    syscall_table[__NR_clock_gettime] = (muslcsys_syscall_t)sys_clock_gettime;
    syscall_table[__NR_ioctl] = (muslcsys_syscall_t)sys_ioctl;
    syscall_table[__NR_writev] = (muslcsys_syscall_t)sys_writev;
    syscall_table[__NR_openat] = (muslcsys_syscall_t)sys_openat;
    syscall_table[__NR_socket] = (muslcsys_syscall_t)sys_socket;
    syscall_table[__NR_fcntl] = (muslcsys_syscall_t)sys_fcntl;
    syscall_table[__NR_bind] = (muslcsys_syscall_t)sys_bind;
    syscall_table[__NR_connect] = (muslcsys_syscall_t)sys_socket_connect;
    syscall_table[__NR_getuid] = (muslcsys_syscall_t)sys_getuid;
    syscall_table[__NR_getgid] = (muslcsys_syscall_t)sys_getgid;
    syscall_table[__NR_setsockopt] = (muslcsys_syscall_t)sys_setsockopt;
    syscall_table[__NR_getsockopt] = (muslcsys_syscall_t)sys_getsockopt;
    syscall_table[__NR_sendto] = (muslcsys_syscall_t)sys_sendto;
    syscall_table[__NR_recvfrom] = (muslcsys_syscall_t)sys_recvfrom;
    syscall_table[__NR_close] = (muslcsys_syscall_t)sys_close;
    syscall_table[__NR_dup3] = (muslcsys_syscall_t)sys_dup3;
    syscall_table[__NR_read] = (muslcsys_syscall_t)sys_read;
    syscall_table[__NR_readv] = (muslcsys_syscall_t)sys_readv;
    syscall_table[__NR_fstat] = (muslcsys_syscall_t)sys_fstat;
    syscall_table[__NR_newfstatat] = (muslcsys_syscall_t)sys_fstatat;
    syscall_table[__NR_readlinkat] = (muslcsys_syscall_t)sys_readlinkat;
    syscall_table[__NR_getrandom] = (muslcsys_syscall_t)sys_getrandom;
}

int socket_index_of_fd(int fd) {
    assert(fd_active[fd]);
    return fd_socket[fd];
}
