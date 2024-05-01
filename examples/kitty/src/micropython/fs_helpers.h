#pragma once

#include <stdint.h>
#include <fs/protocol.h>

#define FS_BUFFER_SIZE 0x8000

int fs_request_allocate(uint64_t *request_id);
void fs_request_free(uint64_t request_id);
void fs_request_flag_set(uint64_t request_id);

int fs_buffer_allocate(ptrdiff_t *buffer);
void fs_buffer_free(ptrdiff_t buffer);
char *fs_buffer_ptr(ptrdiff_t buffer);

void fs_process_completions(void);

void fs_command_issue(uint64_t request_id, uint32_t cmd_type, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);
void fs_command_complete(uint64_t request_id, struct sddf_fs_command *command, struct sddf_fs_completion *completion);
int fs_command_blocking(struct sddf_fs_completion *completion, uint32_t cmd_type, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);
