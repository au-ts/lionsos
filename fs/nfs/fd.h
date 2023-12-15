/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>

#define MAX_OPEN_FILES 256

typedef uint64_t fd_t;

struct nfsfh;
struct nfsdir;

int fd_alloc(fd_t *fd);
int fd_free(fd_t fd);
int fd_set_file(fd_t fd, struct nfsfh *file);
int fd_set_dir(fd_t fd, struct nfsdir *dir);
int fd_unset(fd_t fd);
int fd_begin_op_file(fd_t fd, struct nfsfh **file);
int fd_begin_op_dir(fd_t fd, struct nfsdir **dir);
void fd_end_op(fd_t fd);
