/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>
#include <lions/posix/fd.h>

#include <microkit.h>
#include <libmicrokitco.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_tx_queue_handle;
serial_queue_handle_t serial_rx_queue_handle;

bool fs_enabled;
bool serial_rx_enabled;

#define LIBC_COTHREAD_STACK_SIZE 0x10000
static char libc_cothread_stack[LIBC_COTHREAD_STACK_SIZE];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

#define TEST_COMPONENT "file"
#include "test_helpers.h"

#define TEST_FILE "/test.txt"
#define TEST_DIR "/testdir"

static bool test_openat() {
    int fd = -1;
    bool result = false;

    printf("Open NULL path fails with EINVAL...");
    EXPECT_ERR(openat(AT_FDCWD, NULL, O_RDONLY, 0), EINVAL);
    printf("OK\n");

    printf("Open path > PATH_MAX fails with ENAMETOOLONG...");
    char long_path[4097];
    memset(long_path, 'a', 4096);
    long_path[4096] = '\0';
    EXPECT_ERR(openat(AT_FDCWD, long_path, O_RDONLY, 0), ENAMETOOLONG);
    printf("OK\n");

    printf("Open nonexistent fails with ENOENT...");
    EXPECT_ERR(openat(AT_FDCWD, "/nonexistent", O_RDONLY, 0), ENOENT);
    printf("OK\n");

    printf("Open with O_CREAT creates file...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    close(fd);
    fd = -1;
    printf("OK\n");

    printf("Open existing file succeeds...");
    fd = openat(AT_FDCWD, TEST_FILE, O_RDONLY, 0);
    EXPECT_OK(fd >= 0);
    close(fd);
    fd = -1;
    printf("OK\n");

    printf("Open O_DIRECTORY on file fails with ENOTDIR...");
    EXPECT_ERR(openat(AT_FDCWD, TEST_FILE, O_RDONLY | O_DIRECTORY, 0), ENOTDIR);
    printf("OK\n");

    printf("openat O_WRONLY on directory fails with EISDIR...");
    mkdirat(AT_FDCWD, TEST_DIR, 0755);
    EXPECT_ERR(openat(AT_FDCWD, TEST_DIR, O_WRONLY, 0), EISDIR);
    unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR);
    printf("OK\n");

    printf("openat O_CREAT|O_EXCL on existing file fails with EEXIST...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    close(fd);
    EXPECT_ERR(openat(AT_FDCWD, TEST_FILE, O_CREAT | O_EXCL | O_RDWR, 0644), EEXIST);
    unlinkat(AT_FDCWD, TEST_FILE, 0);
    fd = -1;
    printf("OK\n");

    printf("Open /etc/services returns SERVICES_FD...");
    fd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(fd == SERVICES_FD);
    close(fd);
    fd = -1;
    printf("OK\n");

    printf("Open with bad dirfd fails with EBADF...");
    EXPECT_ERR(openat(-2, TEST_FILE, O_RDONLY, 0), EBADF);
    printf("OK\n");

    printf("Exhausting FD table in openat fails with EMFILE...");
    int fds[MAX_FDS];
    int allocated = 0;
    for (allocated = 0; allocated < MAX_FDS; allocated++) {
        fds[allocated] = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
        if (fds[allocated] < 0) {
            EXPECT_OK(errno == EMFILE);
            break;
        }
    }
    for (int i = 0; i < allocated; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
    }
    return result;
}

static bool test_file_io() {
    int fd = -1;
    bool result = false;
    char buf[64];
    const char *data = "Hello LionsOS!";
    ssize_t n;

    printf("Create test file for IO...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    EXPECT_OK(fd >= 0);
    printf("OK\n");

    printf("write(fd, data) returns bytes written");
    n = write(fd, data, strlen(data));
    EXPECT_OK(n == (ssize_t)strlen(data));
    printf("OK\n");

    printf("lseek(SEEK_SET, 0) returns 0...");
    EXPECT_OK(lseek(fd, 0, SEEK_SET) == 0);
    printf("OK\n");

    printf("read(fd, buf) returns data matches...");
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, strlen(data));
    EXPECT_OK(n == (ssize_t)strlen(data));
    EXPECT_OK(strcmp(buf, data) == 0);
    printf("OK\n");

    printf("lseek(SEEK_CUR, +10) advances...");
    off_t pos = lseek(fd, 0, SEEK_CUR);
    EXPECT_OK(lseek(fd, 10, SEEK_CUR) == pos + 10);
    printf("OK\n");

    printf("lseek(SEEK_END, -1) returns before EOF...");
    EXPECT_OK(lseek(fd, -1, SEEK_END) == (off_t)strlen(data) - 1);
    printf("OK\n");

    printf("lseek(SEEK_SET, -1) fails with EINVAL...");
    EXPECT_ERR(lseek(fd, -1, SEEK_SET), EINVAL);
    printf("OK\n");

    printf("lseek with bad whence fails with EINVAL...");
    EXPECT_ERR(lseek(fd, 0, 999), EINVAL);
    printf("OK\n");

    printf("lseek on bad FD fails with EBADF...");
    EXPECT_ERR(lseek(-1, 0, SEEK_SET), EBADF);
    printf("OK\n");

    printf("lseek on SERVICES_FD fails with EBADF...");
    int sfd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(sfd >= 0);
    EXPECT_ERR(lseek(sfd, 0, SEEK_SET), EBADF);
    close(sfd);
    printf("OK\n");

    printf("read 0 bytes returns 0...");
    EXPECT_OK(read(fd, buf, 0) == 0);
    printf("OK\n");

    printf("write 0 bytes returns 0...");
    EXPECT_OK(write(fd, data, 0) == 0);
    printf("OK\n");

    printf("read NULL buf fails with EFAULT...");
    EXPECT_ERR(read(fd, NULL, 1), EFAULT);
    printf("OK\n");

    printf("write NULL buf fails with EFAULT...");
    EXPECT_ERR(write(fd, NULL, 1), EFAULT);
    printf("OK\n");

    printf("read bad FD fails with EBADF...");
    EXPECT_ERR(read(-1, buf, 1), EBADF);
    printf("OK\n");

    printf("write bad FD fails with EBADF...");
    EXPECT_ERR(write(-1, data, 1), EBADF);
    printf("OK\n");

    printf("lseek large offset fails with EOVERFLOW...");
    close(fd);
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    lseek(fd, 1, SEEK_SET);
    EXPECT_ERR(lseek(fd, LONG_MAX, SEEK_CUR), EOVERFLOW);
    close(fd);
    unlinkat(AT_FDCWD, TEST_FILE, 0);
    fd = -1;
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
        unlinkat(AT_FDCWD, TEST_FILE, 0);
    }
    return result;
}

static bool test_readv_writev() {
    int fd = -1;
    bool result = false;
    struct iovec iov[2];
    char buf1[8], buf2[8];
    const char *data1 = "Hello ";
    const char *data2 = "World!";
    ssize_t n;

    printf("writev with multiple iovecs...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    EXPECT_OK(fd >= 0);
    iov[0].iov_base = (void *)data1;
    iov[0].iov_len = strlen(data1);
    iov[1].iov_base = (void *)data2;
    iov[1].iov_len = strlen(data2);
    n = writev(fd, iov, 2);
    EXPECT_OK(n == (ssize_t)(strlen(data1) + strlen(data2)));
    printf("OK\n");

    printf("readv across multiple iovecs...");
    lseek(fd, 0, SEEK_SET);
    memset(buf1, 0, sizeof(buf1));
    memset(buf2, 0, sizeof(buf2));
    iov[0].iov_base = buf1;
    iov[0].iov_len = 6;
    iov[1].iov_base = buf2;
    iov[1].iov_len = 6;
    n = readv(fd, iov, 2);
    EXPECT_OK(n == 12);
    EXPECT_OK(strncmp(buf1, data1, 6) == 0);
    EXPECT_OK(strncmp(buf2, data2, 6) == 0);
    printf("OK\n");

    printf("readv/writev with NULL iov fails with EFAULT...");
    EXPECT_ERR(readv(fd, NULL, 1), EFAULT);
    EXPECT_ERR(writev(fd, NULL, 1), EFAULT);
    printf("OK\n");

    printf("readv/writev with iovcnt <= 0 fails with EINVAL...");
    EXPECT_ERR(readv(fd, iov, 0), EINVAL);
    EXPECT_ERR(writev(fd, iov, 0), EINVAL);
    EXPECT_ERR(readv(fd, iov, -1), EINVAL);
    EXPECT_ERR(writev(fd, iov, -1), EINVAL);
    printf("OK\n");

    printf("readv/writev with iovcnt > IOV_MAX fails with EINVAL...");
    EXPECT_ERR(readv(fd, iov, IOV_MAX + 1), EINVAL);
    EXPECT_ERR(writev(fd, iov, IOV_MAX + 1), EINVAL);
    printf("OK\n");

    printf("readv/writev with NULL iov_base in non-zero-len vec fails with EFAULT...");
    iov[0].iov_base = NULL;
    iov[0].iov_len = 1;
    EXPECT_ERR(readv(fd, iov, 1), EFAULT);
    EXPECT_ERR(writev(fd, iov, 1), EFAULT);
    printf("OK\n");

    printf("readv on SERVICES_FD succeeds (EOF/0)...");
    int sfd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(sfd >= 0);
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1);
    EXPECT_OK(readv(sfd, iov, 1) == 0);
    printf("OK\n");

    printf("writev on SERVICES_FD fails with EBADF...");
    EXPECT_ERR(writev(sfd, iov, 1), EBADF);
    close(sfd);
    printf("OK\n");

    printf("readv bad FD fails with EBADF...");
    EXPECT_ERR(readv(-1, iov, 1), EBADF);
    printf("OK\n");

    printf("writev bad FD fails with EBADF...");
    EXPECT_ERR(writev(-1, iov, 1), EBADF);
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
        unlinkat(AT_FDCWD, TEST_FILE, 0);
    }
    return result;
}

static bool test_close() {
    int fd = -1;
    bool result = false;

    printf("close valid FD succeeds...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    EXPECT_OK(close(fd) == 0);
    fd = -1;
    printf("OK\n");

    printf("close invalid FD fails with EBADF...");
    EXPECT_ERR(close(-1), EBADF);
    printf("OK\n");

    printf("close /etc/services succeeds...");
    fd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(fd >= 0);
    EXPECT_OK(close(fd) == 0);
    fd = -1;
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
    }
    unlinkat(AT_FDCWD, TEST_FILE, 0);
    return result;
}

static bool test_dup3() {
    int fd = -1, fd2 = -1;
    bool result = false;
    char buf[16];

    printf("dup3 to new FD succeeds...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    fd2 = MAX_FDS - 1;
    EXPECT_OK(dup3(fd, fd2, 0) == fd2);
    write(fd, "test", 4);
    lseek(fd2, 0, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    EXPECT_OK(read(fd2, buf, 4) == 4);
    EXPECT_OK(strcmp(buf, "test") == 0);
    printf("OK\n");

    printf("dup3 to self fails with EINVAL...");
    EXPECT_ERR(dup3(fd, fd, 0), EINVAL);
    printf("OK\n");

    printf("dup3 invalid oldfd fails with EBADF...");
    EXPECT_ERR(dup3(-1, MAX_FDS - 2, 0), EBADF);
    printf("OK\n");

    printf("dup3 invalid newfd fails with EBADF...");
    EXPECT_ERR(dup3(fd, -1, 0), EBADF);
    EXPECT_ERR(dup3(fd, MAX_FDS, 0), EBADF);
    printf("OK\n");

    printf("dup3 with O_CLOEXEC flag set...");
    EXPECT_OK(dup3(fd, MAX_FDS - 2, O_CLOEXEC) == MAX_FDS - 2);
    EXPECT_OK(fcntl(MAX_FDS - 2, F_GETFD, 0) & FD_CLOEXEC);
    close(MAX_FDS - 2);
    printf("OK\n");

    printf("dup3 with invalid flags fails with EINVAL...");
    EXPECT_ERR(dup3(fd, MAX_FDS - 2, 0xFFFF), EINVAL);
    printf("OK\n");

    printf("dup3 involves SERVICES_FD fails with EBADF...");
    int sfd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(sfd >= 0);
    EXPECT_ERR(dup3(sfd, MAX_FDS - 2, 0), EBADF);
    EXPECT_ERR(dup3(fd, sfd, 0), EBADF);
    close(sfd);
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
    }
    if (fd2 >= 0) {
        close(fd2);
    }
    unlinkat(AT_FDCWD, TEST_FILE, 0);
    return result;
}

static bool test_fstat() {
    int fd = -1;
    bool result = false;
    struct stat st;

    printf("fstat file succeeds...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    EXPECT_OK(fstat(fd, &st) == 0);
    EXPECT_OK(S_ISREG(st.st_mode));
    printf("OK\n");

    printf("fstat invalid FD fails with EBADF...");
    EXPECT_ERR(fstat(-1, &st), EBADF);
    printf("OK\n");

    printf("fstat SERVICES_FD returns minimal struct...");
    int sfd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(sfd >= 0);
    EXPECT_OK(fstat(sfd, &st) == 0);
    close(sfd);
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
    }
    unlinkat(AT_FDCWD, TEST_FILE, 0);
    return result;
}

static bool test_fstat_fcntl_ioctl() {
    int fd = -1;
    bool result = false;

    printf("fcntl F_GETFL/F_SETFL O_NONBLOCK...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    int flags = fcntl(fd, F_GETFL, 0);
    EXPECT_OK(flags >= 0);
    EXPECT_OK(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
    EXPECT_OK(fcntl(fd, F_GETFL, 0) & O_NONBLOCK);
    printf("OK\n");

    printf("fcntl unknown op fails with EINVAL...");
    EXPECT_ERR(fcntl(fd, 9999, 0), EINVAL);
    printf("OK\n");

    printf("ioctl stdout succeeds...");
    EXPECT_OK(ioctl(1, 0, NULL) == 0);
    printf("OK\n");

    printf("ioctl file fails with EINVAL...");
    EXPECT_ERR(ioctl(fd, 0, NULL), EINVAL);
    printf("OK\n");

    printf("fcntl/ioctl on bad FD fails with EBADF...");
    EXPECT_ERR(fcntl(-1, F_GETFL, 0), EBADF);
    EXPECT_ERR(ioctl(-1, 0, NULL), EBADF);
    printf("OK\n");

    printf("fcntl/ioctl on SERVICES_FD...");
    int sfd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(sfd >= 0);
    EXPECT_OK(fcntl(sfd, F_GETFL, 0) == 0);
    EXPECT_ERR(ioctl(sfd, 0, NULL), ENOTTY);
    close(sfd);
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
    }
    unlinkat(AT_FDCWD, TEST_FILE, 0);
    return result;
}

static bool test_directory() {
    bool result = false;
    struct stat st;
    int fd = -1;

    // Clean up any leftover state from previous test runs
    unlinkat(AT_FDCWD, "/testdir/file.txt", 0);
    unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR);

    printf("mkdirat new dir succeeds...");
    EXPECT_OK(mkdirat(AT_FDCWD, TEST_DIR, 0755) == 0);
    printf("OK\n");

    printf("fstatat dir succeeds...");
    EXPECT_OK(fstatat(AT_FDCWD, TEST_DIR, &st, 0) == 0);
    EXPECT_OK(S_ISDIR(st.st_mode));
    printf("OK\n");

    printf("fstatat nonexistent file fails with ENOENT...");
    EXPECT_ERR(fstatat(AT_FDCWD, "/nonexistent", &st, 0), ENOENT);
    printf("OK\n");

    printf("unlinkat file succeeds...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    close(fd);
    EXPECT_OK(unlinkat(AT_FDCWD, TEST_FILE, 0) == 0);
    printf("OK\n");

    printf("unlinkat with AT_REMOVEDIR succeeds...");
    EXPECT_OK(unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR) == 0);
    printf("OK\n");

    printf("readlinkat returns EINVAL stub...");
    EXPECT_ERR(readlinkat(AT_FDCWD, "/", NULL, 0), EINVAL);
    printf("OK\n");

    printf("mkdirat existing directory fails...");
    mkdirat(AT_FDCWD, TEST_DIR, 0755);
    EXPECT_ERR(mkdirat(AT_FDCWD, TEST_DIR, 0755), EEXIST);
    printf("OK\n");

    printf("mkdirat/unlinkat/fstatat path too long fails with ENAMETOOLONG...");
    char long_path[4097];
    memset(long_path, 'a', 4096);
    long_path[4096] = '\0';
    EXPECT_ERR(mkdirat(AT_FDCWD, long_path, 0755), ENAMETOOLONG);
    EXPECT_ERR(unlinkat(AT_FDCWD, long_path, 0), ENAMETOOLONG);
    EXPECT_ERR(fstatat(AT_FDCWD, long_path, &st, 0), ENAMETOOLONG);
    printf("OK\n");

    printf("mkdirat/unlinkat/fstatat with bad dirfd fails with EBADF...");
    EXPECT_ERR(mkdirat(-2, TEST_DIR, 0755), EBADF);
    EXPECT_ERR(unlinkat(-2, TEST_DIR, 0), EBADF);
    EXPECT_ERR(fstatat(-2, TEST_DIR, &st, 0), EBADF);
    printf("OK\n");

    printf("mkdirat/unlinkat with NULL path fails with EINVAL...");
    EXPECT_ERR(mkdirat(AT_FDCWD, NULL, 0755), EINVAL);
    EXPECT_ERR(unlinkat(AT_FDCWD, NULL, 0), EINVAL);
    printf("OK\n");

    printf("unlinkat directory without AT_REMOVEDIR fails with EISDIR...");
    mkdirat(AT_FDCWD, TEST_DIR, 0755);
    EXPECT_ERR(unlinkat(AT_FDCWD, TEST_DIR, 0), EISDIR);
    unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR);
    printf("OK\n");

    printf("unlinkat /etc/services fails with EPERM...");
    EXPECT_ERR(unlinkat(AT_FDCWD, "/etc/services", 0), EPERM);
    printf("OK\n");

    printf("unlinkat component not dir fails with ENOTDIR...");
    fd = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    close(fd);
    EXPECT_ERR(unlinkat(AT_FDCWD, "/test.txt/foo", 0), ENOTDIR);
    unlinkat(AT_FDCWD, TEST_FILE, 0);
    printf("OK\n");

    printf("unlinkat non-empty dir fails with ENOTEMPTY...");
    mkdirat(AT_FDCWD, TEST_DIR, 0755);
    fd = openat(AT_FDCWD, "/testdir/file.txt", O_CREAT | O_RDWR, 0644);
    EXPECT_OK(fd >= 0);
    close(fd);
    EXPECT_ERR(unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR), ENOTEMPTY);
    unlinkat(AT_FDCWD, "/testdir/file.txt", 0);
    unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR);
    printf("OK\n");

    result = true;
cleanup:
    unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR);
    return result;
}

static bool run_lifecycle_tests_on(const char *path) {
    int fd = -1;
    bool result = false;
    char buf[64];
    const char *data = "Persistence Test Data";
    struct stat st;

    printf("  Persistence test on %s (write-close-reopen-read)...", path);
    fd = openat(AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    EXPECT_OK(fd >= 0);
    EXPECT_OK(write(fd, data, strlen(data)) == (ssize_t)strlen(data));
    close(fd);

    fd = openat(AT_FDCWD, path, O_RDONLY, 0);
    EXPECT_OK(fd >= 0);
    memset(buf, 0, sizeof(buf));
    EXPECT_OK(read(fd, buf, sizeof(buf)) == (ssize_t)strlen(data));
    EXPECT_OK(strcmp(buf, data) == 0);
    close(fd);
    unlinkat(AT_FDCWD, path, 0);
    fd = -1;
    printf("OK\n");

    printf("  Random access test on %s (seek-overwrite-verify)...", path);
    fd = openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    EXPECT_OK(fd >= 0);
    EXPECT_OK(write(fd, "0123456789", 10) == 10);
    EXPECT_OK(lseek(fd, 2, SEEK_SET) == 2);
    EXPECT_OK(write(fd, "AB", 2) == 2);
    EXPECT_OK(lseek(fd, 6, SEEK_SET) == 6);
    EXPECT_OK(write(fd, "CD", 2) == 2);
    EXPECT_OK(lseek(fd, 0, SEEK_SET) == 0);
    memset(buf, 0, sizeof(buf));
    EXPECT_OK(read(fd, buf, 10) == 10);
    EXPECT_OK(strncmp(buf, "01AB45CD89", 10) == 0);
    close(fd);
    unlinkat(AT_FDCWD, path, 0);
    fd = -1;
    printf("OK\n");

    printf("  Truncation test on %s...", path);
    fd = openat(AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    EXPECT_OK(fd >= 0);
    EXPECT_OK(write(fd, "Initial Content", 15) == 15);
    close(fd);
    fd = openat(AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    EXPECT_OK(fd >= 0);
    close(fd);
    EXPECT_OK(fstatat(AT_FDCWD, path, &st, 0) == 0);
    EXPECT_OK(st.st_size == 0);
    unlinkat(AT_FDCWD, path, 0);
    fd = -1;
    printf("OK\n");

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
        unlinkat(AT_FDCWD, path, 0);
    }
    return result;
}

static bool test_file_lifecycle() {
    bool result = false;

    printf("POSIX_TEST|file|INFO|Running lifecycle tests in root directory\n");
    if (!run_lifecycle_tests_on("/test.txt")) {
        return false;
    }

    printf("POSIX_TEST|file|INFO|Running lifecycle tests in nested directory\n");
    // Setup nested directory structure
    unlinkat(AT_FDCWD, "/testdir/sub/test.txt", 0);
    unlinkat(AT_FDCWD, "/testdir/sub", AT_REMOVEDIR);
    unlinkat(AT_FDCWD, "/testdir", AT_REMOVEDIR);

    EXPECT_OK(mkdirat(AT_FDCWD, "/testdir", 0755) == 0);
    EXPECT_OK(mkdirat(AT_FDCWD, "/testdir/sub", 0755) == 0);

    if (!run_lifecycle_tests_on("/testdir/sub/test.txt")) {
        goto cleanup;
    }

    // Final cleanup
    EXPECT_OK(unlinkat(AT_FDCWD, "/testdir/sub", AT_REMOVEDIR) == 0);
    EXPECT_OK(unlinkat(AT_FDCWD, "/testdir", AT_REMOVEDIR) == 0);

    result = true;
cleanup:
    return result;
}

void run_tests(void) {
    printf("POSIX_TEST|file|START\n");

    if (!test_openat()) {
        return;
    }

    if (!test_file_io()) {
        return;
    }

    if (!test_readv_writev()) {
        return;
    }

    if (!test_close()) {
        return;
    }

    if (!test_dup3()) {
        return;
    }

    if (!test_fstat()) {
        return;
    }

    if (!test_fstat_fcntl_ioctl()) {
        return;
    }

    if (!test_directory()) {
        return;
    }

    if (!test_file_lifecycle()) {
        return;
    }

    printf("POSIX_TEST|file|PASS\n");
}

void cont(void) {
    libc_init(NULL);

    if (fs_enabled) {
        fs_cmpl_t completion;
        int err = fs_command_blocking(&completion, (fs_cmd_t) { .type = FS_CMD_INITIALISE });
        if (err || completion.status != FS_STATUS_SUCCESS) {
            printf("POSIX_TEST|file|ERROR|Failed to mount filesystem\n");
            return;
        }

        run_tests();
    } else {
        printf("POSIX_TEST|file|SKIP|Filesystem not enabled\n");
    }
}

void notified(microkit_channel ch) {
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);
}

void init(void) {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    fs_enabled = fs_config_check_magic(&fs_config);

    serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);
    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    if (fs_enabled) {
        fs_set_blocking_wait(blocking_wait);
        fs_command_queue = fs_config.server.command_queue.vaddr;
        fs_completion_queue = fs_config.server.completion_queue.vaddr;
        fs_share = fs_config.server.share.vaddr;
    }

    stack_ptrs_arg_array_t costacks = { (uintptr_t)libc_cothread_stack };
    microkit_cothread_init(&co_controller_mem, LIBC_COTHREAD_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(cont, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("POSIX_TEST|file|ERROR|Cannot initialise cothread\n");
        assert(false);
    }

    microkit_cothread_yield();
}
