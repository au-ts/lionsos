#pragma once

#include <stdint.h>
#include <lions/fs/protocol.h>

#define FS_BUFFER_SIZE 0x8000

int fs_request_allocate(uint64_t *request_id);
void fs_request_free(uint64_t request_id);
void fs_request_flag_set(uint64_t request_id);

int fs_buffer_allocate(ptrdiff_t *buffer);
void fs_buffer_free(ptrdiff_t buffer);
void *fs_buffer_ptr(ptrdiff_t buffer);

void fs_process_completions(void);

void fs_command_issue(fs_cmd_t cmd);
void fs_command_complete(uint64_t request_id, fs_cmd_t *cmd, fs_cmpl_t *cmpl);
int fs_command_blocking(fs_cmpl_t *cmpl, fs_cmd_t cmd);
