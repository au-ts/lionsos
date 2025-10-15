#include <assert.h>
#include <bits/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>

#include <lions/posix/posix.h>

#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/config.h>

#define MAX_FDS 128
#define MAX_PATH_LEN 128

static int fd_active[MAX_FDS];
static int fd_flags[MAX_FDS];
static char fd_path[MAX_FDS][MAX_PATH_LEN];

#define STDOUT_FD 1
#define STDERR_FD 2

extern serial_queue_handle_t serial_tx_queue_handle;
extern serial_client_config_t serial_config;

size_t file_ptrs[MAX_FDS];

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
                                                        .offset = file_ptrs[fd],
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

        assert(fd < MAX_FDS);
        file_ptrs[fd] += read;
    }

    return ret;
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

    /* Write the buffer to console if the fd is for stdout or stderr. */
    if (fildes == STDOUT_FD || fildes == STDERR_FD) {
        for (int i = 0; i < iovcnt; i++) {
            ret += output(iov[i].iov_base, iov[i].iov_len);
        }
    } else {
        for (int i = 0; i < iovcnt; i++) {
            if (iov[i].iov_len == 0) {
                continue;
            }
            ptrdiff_t write_buffer;
            fs_buffer_allocate(&write_buffer);

            memcpy(fs_buffer_ptr(write_buffer), iov[i].iov_base, iov[i].iov_len);

            fs_cmpl_t completion;
            fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_FILE_WRITE,
                                                              .params.file_write = {
                                                                  .fd = fildes,
                                                                  .offset = file_ptrs[fildes],
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
            assert(fildes < MAX_FDS);
            file_ptrs[fildes] += wrote;
        }
    }
    return ret;
}

static int request_flags[FS_QUEUE_CAPACITY];

void posix_fs_request_flag_set(uint64_t request_id) { request_flags[request_id] = 0; }

long sys_openat(va_list ap) {
    int dirfd = va_arg(ap, int);
    const char *path = va_arg(ap, const char *);
    int flags = va_arg(ap, int);

    (void)dirfd;

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

long sys_fcntl(va_list ap) {
    int fd = va_arg(ap, int);
    int op = va_arg(ap, int);

    switch (op) {
        case F_GETFL:
            return fd_flags[fd];
            break;
    }

    return 0;
}

long sys_close(va_list ap) {
    long fd = va_arg(ap, int);

    if (fd == SERVICES_FD) {
        return 0;
    }

    assert(fd_active[fd]);
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

    return 0;
}

long sys_lseek(va_list ap) {
    long fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    int whence = va_arg(ap, int);

    /* TODO: need to sanitise input */
    size_t curr_fp = file_ptrs[fd];
    size_t new_fp;
    switch (whence) {
    case SEEK_SET:
        new_fp = offset;
        break;
    case SEEK_CUR:
        new_fp = curr_fp + offset;
        break;
    default:
        printf("POSIX ERROR: lseek got unsupported whence %d\n", whence);
        // TODO: correct error
        return -1;
    }

    file_ptrs[fd] = new_fp;

    return 0;
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

    statbuf->st_dev = sb->dev;
    statbuf->st_ino = sb->ino;
    statbuf->st_mode = sb->mode;
    statbuf->st_nlink = sb->nlink;
    statbuf->st_uid = sb->uid;
    statbuf->st_gid = sb->gid;
    statbuf->st_rdev = sb->rdev;
    statbuf->st_size = sb->size;
    statbuf->st_blksize = sb->blksize;
    statbuf->st_blocks = sb->blocks;
    statbuf->st_atime = sb->atime;
    statbuf->st_mtime = sb->mtime;
    statbuf->st_ctime = sb->ctime;
    statbuf->st_atim.tv_nsec = sb->atime_nsec;
    statbuf->st_mtim.tv_nsec = sb->mtime_nsec;
    statbuf->st_ctim.tv_nsec = sb->ctime_nsec;
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

void libc_init_file() {
    libc_define_syscall(__NR_write, (muslcsys_syscall_t)sys_write);
    libc_define_syscall(__NR_writev, (muslcsys_syscall_t)sys_writev);
    libc_define_syscall(__NR_openat, (muslcsys_syscall_t)sys_openat);
    libc_define_syscall(__NR_fcntl, (muslcsys_syscall_t)sys_fcntl);
    libc_define_syscall(__NR_close, (muslcsys_syscall_t)sys_close);
    libc_define_syscall(__NR_lseek, (muslcsys_syscall_t)sys_lseek);
    libc_define_syscall(__NR_read, (muslcsys_syscall_t)sys_read);
    libc_define_syscall(__NR_readv, (muslcsys_syscall_t)sys_readv);
    libc_define_syscall(__NR_fstat, (muslcsys_syscall_t)sys_fstat);
    libc_define_syscall(__NR_newfstatat, (muslcsys_syscall_t)sys_fstatat);
    libc_define_syscall(__NR_readlinkat, (muslcsys_syscall_t)sys_readlinkat);
}
