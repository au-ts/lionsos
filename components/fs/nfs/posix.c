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
#include <sys/syscall.h>
#include <sddf/serial/queue.h>

#include "nfs.h"
#include "posix.h"
#include "tcp.h"
#include "util.h"

#define MUSLC_HIGHEST_SYSCALL SYS_pkey_free
#define MUSLC_NUM_SYSCALLS (MUSLC_HIGHEST_SYSCALL + 1)
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON

#define STDOUT_FD 1
#define STDERR_FD 2
#define LWIP_FD_START 3

typedef long (*muslcsys_syscall_t)(va_list);

extern void *__sysinfo;

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

static size_t output(void *data, size_t count)
{
    uint32_t *tail = &serial_tx_queue_handle.queue->tail;
    uint32_t sent = 0;
    char *string = data;

    while (!serial_queue_full(&serial_tx_queue_handle, *tail)
                                && sent < count) {
        char c = string[sent];
        if (c != '\n') {
            serial_enqueue(&serial_tx_queue_handle, tail, c);
        } else {
            /* Ensure buffer can fit \r and \n */
            if (serial_queue_full(&serial_tx_queue_handle,
                                  *tail + 1)) {
                break;
            }
            serial_enqueue(&serial_tx_queue_handle, tail, '\r');
            serial_enqueue(&serial_tx_queue_handle, tail, c);
        }
        sent++;
    }

    if (sent && serial_require_producer_signal(&serial_tx_queue_handle)) {
        serial_cancel_producer_signal(&serial_tx_queue_handle);
        microkit_notify(SERIAL_TX_CH);
    }

    return sent;
}

long sys_brk(va_list ap)
{
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    /* if the newbrk is 0, return the bottom of the heap */
    if (!newbrk) {
        return morecore_base;
    } else if (newbrk < morecore_top && newbrk > (uintptr_t)&morecore_area[0]) {
        return morecore_base = newbrk;
    }
    return 0;
}

uintptr_t align_addr(uintptr_t addr)
{
    return (addr + 0xfff) & ~0xfff;
}

long sys_mmap(va_list ap)
{
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    (void)fd, (void)offset, (void)prot, (void)addr;

    if (flags & MAP_ANONYMOUS)
    {
        /* Check that we don't try and allocate more than exists */
        if (length > morecore_top - morecore_base)
        {
            return -ENOMEM;
        }
        /* Steal from the top */
        morecore_top -= length;
        return morecore_top;
    }
    return -ENOMEM;
}

long sys_madvise(va_list ap)
{
    return 0;
}

long sys_write(va_list ap)
{
    int fd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t count = va_arg(ap, size_t);

    if (fd == 1 || fd == 2)
    {
        uint32_t n = serial_enqueue_batch(&serial_tx_queue_handle, count, buf);

        if (n && serial_require_producer_signal(&serial_tx_queue_handle)) {
            serial_cancel_producer_signal(&serial_tx_queue_handle);
            microkit_notify(SERIAL_TX_CH);
        }

        return n;
    }

    return -1;
}

long sys_clock_gettime(va_list ap)
{
    clockid_t clk_id = va_arg(ap, clockid_t);
    struct timespec *tp = va_arg(ap, struct timespec *);

    uint64_t rtc = 0;

    tp->tv_sec = rtc / 1000;
    tp->tv_nsec = (rtc % 1000) * 1000000;

    return 0;
}

long sys_getpid(va_list ap)
{
    return 0;
}

long sys_ioctl(va_list ap)
{
    int fd = va_arg(ap, int);
    int request = va_arg(ap, int);
    (void)request;
    dlog("musl called ioctl on fd %d", fd);
    /* muslc does some ioctls to stdout, so just allow these to silently
       go through */
    if (fd == STDOUT_FD)
    {
        return 0;
    }

    return 0;
}

long sys_writev(va_list ap)
{
    int fildes = va_arg(ap, int);
    struct iovec *iov = va_arg(ap, struct iovec *);
    int iovcnt = va_arg(ap, int);

    long long sum = 0;
    ssize_t ret = 0;

    /* The iovcnt argument is valid if greater than 0 and less than or equal to IOV_MAX. */
    if (iovcnt <= 0 || iovcnt > IOV_MAX)
    {
        return -EINVAL;
    }

    /* The sum of iov_len is valid if less than or equal to SSIZE_MAX i.e. cannot overflow
       a ssize_t. */
    for (int i = 0; i < iovcnt; i++)
    {
        sum += (long long)iov[i].iov_len;
        if (sum > SSIZE_MAX)
        {
            return -EINVAL;
        }
    }

    /* If all the iov_len members in the array are 0, return 0. */
    if (!sum)
    {
        return 0;
    }

    /* Write the buffer to console if the fd is for stdout or stderr. */
    if (fildes == STDOUT_FD || fildes == STDERR_FD)
    {
        for (int i = 0; i < iovcnt; i++)
        {
            ret += output(iov[i].iov_base, iov[i].iov_len);
        }
    } else {
        // fildes must refer to socket
        int socket_handle = fd_socket[fildes];

        assert(socket_handle >= 0);
        assert(socket_handle < MAX_SOCKETS);
        assert(socket_refcount[socket_handle] != 0);

        for (int i = 0; i < iovcnt; i++) {
            int wrote = tcp_socket_write(socket_handle, iov[i].iov_base, iov[i].iov_len);
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

long sys_openat(va_list ap)
{
    (void)ap;
    return ENOSYS;
}

long sys_getuid(va_list ap)
{
    (void)ap;
    return 501;
}

long sys_getgid(va_list ap)
{
    (void)ap;
    return 501;
}

long sys_fcntl(va_list ap)
{
    return 0;
}

long sys_setsockopt(va_list ap)
{
    return 0;
}

long sys_getsockopt(va_list ap)
{
    return 0;
}

long sys_socket(va_list ap)
{
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
    socket_refcount[socket_handle]++;
    
    fd_active[fd] = true;
    fd_socket[fd] = socket_handle;

    return fd;
}

long sys_bind(va_list ap)
{
    return 0;
}

long sys_socket_connect(va_list ap)
{
    long fd = va_arg(ap, int);

    assert(fd_active[fd]);

    int socket_handle = fd_socket[fd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    const struct sockaddr *addr = va_arg(ap, const struct sockaddr *);
    int port = addr->sa_data[0] << 8 | addr->sa_data[1];
    return (long)tcp_socket_connect(socket_handle, port);
}

long sys_close(va_list ap)
{
    long fd = va_arg(ap, int);

    assert(fd_active[fd]);
    
    int socket_handle = fd_socket[fd];

    assert(socket_handle >= 0);
    assert(socket_handle < MAX_SOCKETS);
    assert(socket_refcount[socket_handle] != 0);

    fd_socket[fd] = 0;
    fd_active[fd] = 0;

    socket_refcount[socket_handle]--;
    if (socket_refcount[socket_handle] == 0) {
	return (long)tcp_socket_close(socket_handle);
    }

    return 0;
}

long sys_dup3(va_list ap)
{
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

long sys_sendto(va_list ap)
{
    int sockfd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t len = va_arg(ap, size_t);
    int flags = va_arg(ap, int);

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

long sys_recvfrom(va_list ap)
{
    int sockfd = va_arg(ap, int);
    void *buf = va_arg(ap, void *);
    ssize_t len = va_arg(ap, int);
    int flags = va_arg(ap, int);
    struct sockaddr *src_addr = va_arg(ap, struct sockaddr *);
    socklen_t *addrlen = va_arg(ap, socklen_t *);

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

void debug_error(long num)
{
    dlog("error doing syscall: %d", num);
}

int pthread_setcancelstate(int state, int *oldstate)
{
    (void)state;
    (void)oldstate;
    return 0;
}

long sel4_vsyscall(long sysnum, ...)
{
    va_list al;
    va_start(al, sysnum);
    muslcsys_syscall_t syscall;

    if (sysnum < 0 || sysnum >= ARRAY_SIZE(syscall_table))
    {
        debug_error(sysnum);
        return -ENOSYS;
    }
    else
    {
        syscall = syscall_table[sysnum];
    }
    /* Check a syscall is implemented there */
    if (!syscall)
    {
        debug_error(sysnum);
        return -ENOSYS;
    }
    /* Call it */
    long ret = syscall(al);
    va_end(al);
    return ret;
}

void syscalls_init(void)
{
    /* Syscall table init */
    __sysinfo = sel4_vsyscall;
    syscall_table[__NR_brk] = (muslcsys_syscall_t)sys_brk;
    syscall_table[__NR_write] = (muslcsys_syscall_t)sys_write;
    syscall_table[__NR_mmap] = (muslcsys_syscall_t)sys_mmap;
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
    syscall_table[__NR_getsockopt] = (muslcsys_syscall_t)sys_setsockopt;
    syscall_table[__NR_sendto] = (muslcsys_syscall_t)sys_sendto;
    syscall_table[__NR_recvfrom] = (muslcsys_syscall_t)sys_recvfrom;
    syscall_table[__NR_close] = (muslcsys_syscall_t)sys_close;
    syscall_table[__NR_dup3] = (muslcsys_syscall_t)sys_dup3;
}

int socket_index_of_fd(int fd) {
    assert(fd_active[fd]);
    return fd_socket[fd];
}
