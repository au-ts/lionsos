/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>


uint64_t fs_file_open_blocking(char *path, uint64_t path_len, uint64_t flags);
void fs_file_truncate_blocking(uint64_t fd);
uint64_t fs_file_size_blocking(uint64_t fd);
uint64_t fs_file_write_blocking(uint64_t fd, uint64_t off, void* data, uint64_t data_size_bytes);
uint64_t fs_file_read_blocking(uint64_t fd, uint64_t off, void* data, uint64_t data_size_bytes);
void fs_file_sync_blocking(uint64_t fd);
void fs_file_close_blocking(uint64_t fd);

uint64_t fs_dir_open_blocking(char *path, uint64_t path_len);
uint64_t fs_dir_read_blocking(uint64_t fd, void* data);
uint64_t fs_dir_tell_blocking(uint64_t fd);
void fs_dir_seek_blocking(uint64_t fd, uint64_t loc);
void fs_dir_close_blocking(uint64_t fd);
