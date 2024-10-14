/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>

#include <lions/fs/protocol.h>

#define MAX_OPEN_FILES 256

typedef uint64_t fd_t;

int fd_alloc(fd_t *fd);
int fd_free(fd_t fd);
int fd_set_file(fd_t fd, void *file_handle);
int fd_set_dir(fd_t fd, void *dir_handle);
int fd_unset(fd_t fd);
int fd_begin_op_file(fd_t fd, void **file_handle);
int fd_begin_op_dir(fd_t fd, void **dir_handle);
void fd_end_op(fd_t fd);

void *fs_get_client_buffer(char *client_share, size_t client_share_size, fs_buffer_t buf);
// Assumes dest is at least the size of buf.size + 1; buf.size is bounded from above by FS_MAX_PATH_LENGTH
int fs_copy_client_path(char *dest, char *client_share, size_t client_share_size, fs_buffer_t buf);
