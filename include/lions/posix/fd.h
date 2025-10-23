/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdlib.h>
#include <sys/stat.h>

#define MAX_FDS 128

#define STDOUT_FD 1
#define STDERR_FD 2

typedef size_t (*fd_write_func)(const void *, size_t, int);
typedef size_t (*fd_read_func)(void *, size_t, int);
typedef int (*fd_close_func)(int);
typedef int (*fd_dup3_func)(int, int);
typedef int (*fd_fstat_func)(int, struct stat*);

typedef struct fd_entry {
    fd_write_func write;
    fd_read_func read;
    fd_close_func close;
    fd_dup3_func dup3;
    fd_fstat_func fstat;
    int flags;
    size_t file_ptr;
} fd_entry_t;

int posix_fd_allocate();
int posix_fd_deallocate(int fd);
fd_entry_t *posix_fd_entry(int fd);
