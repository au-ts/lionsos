/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <fat_config.h>
#include "ff.h"
#include <lions/fs/protocol.h>

// Use struct instead of union
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

// For debug
#ifdef FAT_DEBUG_PRINT
#include <sddf/util/printf.h>
#define LOG_FATFS(...) do{ sddf_dprintf("FATFS|INFO: "); sddf_dprintf( __VA_ARGS__); } while(0)
#else
#define LOG_FATFS(...) do{}while(0)
#endif
