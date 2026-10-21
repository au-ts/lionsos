/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <microkit.h>
#include <stddef.h>
#include <stdint.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include <lions/fs/server.h>

#define FS_BUFFER_SIZE 0x8000

int fs_request_allocate(uint64_t *request_id);
void fs_request_free(uint64_t request_id);

int fs_buffer_allocate(ptrdiff_t *buffer);
void fs_buffer_free(ptrdiff_t buffer);
void *fs_buffer_ptr(ptrdiff_t buffer);

void fs_process_completions(void (*fs_request_flag_set)(uint64_t));

void fs_command_issue(fs_cmd_t cmd);
void fs_command_complete(uint64_t request_id, fs_cmd_t *cmd, fs_cmpl_t *cmpl);
void fs_set_blocking_wait(void(*f)(microkit_channel));
int fs_command_blocking(fs_cmpl_t *cmpl, fs_cmd_t cmd);

#ifdef RAMFATFS
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
#endif