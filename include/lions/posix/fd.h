/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

// Allow override of max FDs e.g. for testing purposes
// Add -DMAX_FDS=<value> to CFLAGS to override
#ifndef MAX_FDS
#define MAX_FDS 128
#endif

/*
 * Reserved FDs for special files
 */

// stdio
#define STDIN_FD  0
#define STDOUT_FD 1
#define STDERR_FD 2

// /etc/services
#define SERVICES_FD (MAX_FDS)
#define ETC_FD      (MAX_FDS + 1)

typedef ssize_t (*fd_write_func)(const void *, size_t, int);
typedef ssize_t (*fd_read_func)(void *, size_t, int);
typedef int (*fd_close_func)(int);
typedef int (*fd_dup3_func)(int, int);
typedef int (*fd_fstat_func)(int, struct stat *);

typedef struct {
    fd_write_func write;
    fd_read_func read;
    fd_close_func close;
    fd_dup3_func dup3;
    fd_fstat_func fstat;
    int flags;
    off_t file_ptr;
} fd_entry_t;

/**
 * @brief Allocates a file descriptor from the available pool.
 *
 * @return Index of the allocated file descriptor on success, -1 if none are available.
 */
int posix_fd_allocate();

/**
 * @brief Deallocates a file descriptor and returns it to the available pool.
 *
 * @param fd The file descriptor to deallocate.
 * @return 0 on success, -1 if the file descriptor is invalid.
 */
int posix_fd_deallocate(int fd);

/**
 * @brief Retrieves the file descriptor entry for a given file descriptor.
 *
 * @param fd The file descriptor to look up.
 * @return Pointer to the fd_entry_t structure associated with the file descriptor,
 *         or NULL if the file descriptor is not valid.
 */
fd_entry_t *posix_fd_entry(int fd);

/**
 * @brief Allocates a specific file descriptor and returns its entry.
 *
 * @param fd The file descriptor to allocate. Must not already be allocated.
 * @return Pointer to the fd_entry_t structure associated with the file descriptor,
 *         or NULL if allocation fails.
 */
fd_entry_t *posix_fd_entry_allocate(int fd);
