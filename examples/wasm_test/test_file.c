/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <stdbool.h>

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

    printf("Open /etc/services returns valid FD...");
    fd = openat(AT_FDCWD, "/etc/services", O_RDONLY, 0);
    EXPECT_OK(fd >= 0);
    close(fd);
    fd = -1;
    printf("OK\n");

    printf("Open with bad dirfd fails with EBADF...");
    EXPECT_ERR(openat(-3, "test.txt", O_RDONLY, 0), EBADF);
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

    printf("read 0 bytes returns 0...");
    EXPECT_OK(read(fd, buf, 0) == 0);
    printf("OK\n");

    printf("write 0 bytes returns 0...");
    EXPECT_OK(write(fd, data, 0) == 0);
    printf("OK\n");

    printf("read bad FD fails with EBADF...");
    EXPECT_ERR(read(-1, buf, 1), EBADF);
    printf("OK\n");

    printf("write bad FD fails with EBADF...");
    EXPECT_ERR(write(-1, data, 1), EBADF);
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

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
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
    EXPECT_ERR(mkdirat(-3, "testdir", 0755), EBADF);
    EXPECT_ERR(unlinkat(-3, "testdir", 0), EBADF);
    EXPECT_ERR(fstatat(-3, "testdir", &st, 0), EBADF);
    printf("OK\n");

    printf("unlinkat directory without AT_REMOVEDIR fails with EISDIR...");
    mkdirat(AT_FDCWD, TEST_DIR, 0755);
    EXPECT_ERR(unlinkat(AT_FDCWD, TEST_DIR, 0), EISDIR);
    unlinkat(AT_FDCWD, TEST_DIR, AT_REMOVEDIR);
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

    printf("WASM_TEST|file|INFO|Running lifecycle tests in root directory\n");
    if (!run_lifecycle_tests_on("/test.txt")) {
        goto cleanup;
    }

    printf("WASM_TEST|file|INFO|Running lifecycle tests in nested directory\n");
    // Cleanup any leftover state from previous test runs
    unlinkat(AT_FDCWD, "/testdir/sub/test.txt", 0);
    unlinkat(AT_FDCWD, "/testdir/sub", AT_REMOVEDIR);
    unlinkat(AT_FDCWD, "/testdir", AT_REMOVEDIR);

    EXPECT_OK(mkdirat(AT_FDCWD, "/testdir", 0755) == 0);
    EXPECT_OK(mkdirat(AT_FDCWD, "/testdir/sub", 0755) == 0);

    if (!run_lifecycle_tests_on("/testdir/sub/test.txt")) {
        goto cleanup;
    }

    EXPECT_OK(unlinkat(AT_FDCWD, "/testdir/sub", AT_REMOVEDIR) == 0);
    EXPECT_OK(unlinkat(AT_FDCWD, "/testdir", AT_REMOVEDIR) == 0);

    result = true;

cleanup:
    return result;
}

static bool test_fcntl() {
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

    result = true;
cleanup:
    if (fd >= 0) {
        close(fd);
        unlinkat(AT_FDCWD, TEST_FILE, 0);
    }
    return result;
}

void run_tests(void) {
    printf("WASM_TEST|file|START\n");

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

    if (!test_fcntl()) {
        return;
    }

    if (!test_fstat()) {
        return;
    }

    if (!test_directory()) {
        return;
    }

    if (!test_file_lifecycle()) {
        return;
    }

    printf("WASM_TEST|file|PASS\n");
}

int main(void) {
    run_tests();
    return 0;
}
