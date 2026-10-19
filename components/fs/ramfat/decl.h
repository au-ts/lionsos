/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <fat_config.h>
#include "ff.h"
#include <lions/fs/protocol.h>

int handle_initialise(void);
int handle_deinitialise(void);
int handle_file_open(const char *path, uint64_t flags, fd_t *fd);
int handle_file_close(fd_t fd);
int handle_stat(const char *path, fs_stat_t *stat);
int handle_file_read(fd_t fd, void *buf, uint64_t len, uint64_t offset, uint64_t *len_read);
int handle_file_write(fd_t fd, const void *buf, uint64_t len, uint64_t offset, uint64_t *len_written);
int handle_file_size(fd_t fd, uint64_t *size);
int handle_rename(const char *old_path, const char *new_path);
int handle_file_remove(const char *path);
int handle_file_truncate(fd_t fd, uint64_t length);
int handle_dir_create(const char *path);
int handle_dir_remove(const char *path);
int handle_dir_open(const char *path, fd_t *fd);
int handle_dir_close(fd_t fd);
int handle_file_sync(fd_t fd);
int handle_dir_read(fd_t fd, char *name, uint64_t size, uint64_t *len);
int handle_dir_seek(fd_t fd, int64_t loc);
int handle_dir_tell(fd_t fd, uint64_t *location);
int handle_dir_rewind(fd_t fd);

// For debug
#ifdef FAT_DEBUG_PRINT
#include <sddf/util/printf.h>
#define LOG_FATFS(...) do{ sddf_dprintf("FATFS|INFO: "); sddf_dprintf( __VA_ARGS__); } while(0)
#else
#define LOG_FATFS(...) do{}while(0)
#endif
