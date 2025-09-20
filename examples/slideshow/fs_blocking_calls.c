/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <string.h>
#include <stdbool.h>
#include <sddf/util/printf.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include <libmicrokitco.h>

#include "fs_client_helpers.h"

extern fs_client_config_t fs_config;
extern char *fs_share;

uint64_t fs_file_open_blocking(char *path, uint64_t path_len, uint64_t flags) {
    assert(path);
    assert(path_len < FS_MAX_PATH_LENGTH);

    ptrdiff_t buf_off;
    assert(fs_buffer_allocate(&buf_off) == 0);
    char *buf = (char *) fs_buffer_ptr(buf_off);
    strcpy(buf, path);

    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_OPEN,
        .params.file_open = {
            .path.offset = (uint64_t) buf_off,
            .path.size = path_len,
            .flags = FS_OPEN_FLAGS_CREATE | flags,
        }
    });
    fs_buffer_free(buf_off);
    assert(!err && completion.status == FS_STATUS_SUCCESS);
    return completion.data.file_open.fd;
}

void fs_file_truncate_blocking(uint64_t fd) {
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_TRUNCATE,
        .params.file_truncate = {
            .fd = fd,
        }
    });
    assert(!err && completion.status == FS_STATUS_SUCCESS);
}

uint64_t fs_file_size_blocking(uint64_t fd) {
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_SIZE,
        .params.file_size = {
            .fd = fd,
        }
    });
    assert(!err && completion.status == FS_STATUS_SUCCESS);
    return completion.data.file_size.size;
}

/* Return number of bytes written */
uint64_t fs_file_write_blocking(uint64_t fd, uint64_t off, void* data, uint64_t data_size_bytes) {
    assert(data);
    assert(data_size_bytes <= FS_BUFFER_SIZE);

    ptrdiff_t buf_off;
    assert(fs_buffer_allocate(&buf_off) == 0);
    void *buf = fs_buffer_ptr(buf_off);
    memcpy(buf, data, data_size_bytes);

    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_WRITE,
        .params.file_write = {
            .fd = fd,
            .offset = off,
            .buf.offset = (uint64_t) buf_off,
            .buf.size = data_size_bytes,
        }
    });
    fs_buffer_free(buf_off);
    assert(!err && completion.status == FS_STATUS_SUCCESS);
    // This assert will fail if you run out of space on disk.
    assert(data_size_bytes == completion.data.file_write.len_written);
    return completion.data.file_write.len_written;
}

/* Return number of bytes read */
uint64_t fs_file_read_blocking(uint64_t fd, uint64_t off, void* data, uint64_t data_size_bytes) {
    assert(data);
    assert(data_size_bytes <= FS_BUFFER_SIZE);

    ptrdiff_t buf_off;
    assert(fs_buffer_allocate(&buf_off) == 0);
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_READ,
        .params.file_read = {
            .fd = fd,
            .offset = off,
            .buf.offset = (uint64_t) buf_off,
            .buf.size = data_size_bytes,
        }
    });
    assert(!err && completion.status == FS_STATUS_SUCCESS);
    assert(data_size_bytes == completion.data.file_read.len_read);

    memcpy(data, fs_buffer_ptr(buf_off), data_size_bytes);

    fs_buffer_free(buf_off);
    return completion.data.file_read.len_read;
}

void fs_file_sync_blocking(uint64_t fd) {
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_SYNC,
        .params.file_sync = {
            .fd = fd,
        }
    });
    assert(!err && completion.status == FS_STATUS_SUCCESS);
}

void fs_file_close_blocking(uint64_t fd) {
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_CLOSE,
        .params.file_close = {
            .fd = fd,
        }
    });
    assert(!err && completion.status == FS_STATUS_SUCCESS);
}
