/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <lions/fs/protocol.h>

#include <ext4_component_config.h>
#include <libmicrokitco.h>

typedef struct {
    fs_cmd_params_t params;
    uint64_t status;
    fs_cmpl_data_t result;
} co_data_t;

void handle_initialise(void);
void handle_deinitialise(void);
void handle_file_open(void);
void handle_file_close(void);
void handle_stat(void);
void handle_file_read(void);
void handle_file_write(void);
void handle_file_size(void);
void handle_rename(void);
void handle_file_remove(void);
void handle_file_truncate(void);
void handle_dir_create(void);
void handle_dir_remove(void);
void handle_dir_open(void);
void handle_dir_close(void);
void handle_file_sync(void);
void handle_dir_seek(void);
void handle_dir_read(void);
void handle_dir_rewind(void);
void handle_dir_tell(void);

extern microkit_cothread_sem_t sem[EXT4_THREAD_NUM];

static inline void wait_for_blk_resp(void) {
    microkit_cothread_ref_t handle = microkit_cothread_my_handle();
    microkit_cothread_semaphore_wait(&sem[handle]);
}

#ifdef EXT4_DEBUG_PRINT
#include <sddf/util/printf.h>
#define LOG_EXT4FS(...) do { sddf_dprintf("EXT4FS|INFO: "); sddf_dprintf(__VA_ARGS__); } while (0)
#else
#define LOG_EXT4FS(...) do {} while (0)
#endif
