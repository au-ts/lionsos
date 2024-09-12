#pragma once

#include "fatfs_config.h"
#include "ff15/source/ff.h"
#include <fs/protocol.h>

// Use struct instead of union
typedef struct {
    fs_cmd_params_t params;
    uint64_t status;
    fs_cmpl_data_t result;
} co_data_t;

void init_metadata(uint64_t fs_metadata);

void fat_mount(void);
void fat_unmount(void);
void fat_open(void);
void fat_close(void);
void fat_stat(void);
void fat_pread(void);
void fat_pwrite(void);
void fat_fsize(void);
void fat_rename(void);
void fat_unlink(void);
void fat_truncate(void);
void fat_mkdir(void);
void fat_rmdir(void);
void fat_opendir(void);
void fat_closedir(void);
void fat_sync(void);
void fat_seekdir(void);
void fat_readdir(void);
void fat_rewinddir(void);
void fat_telldir(void);

// For debug
#ifdef FS_DEBUG_PRINT
#include <sddf/util/printf.h>
#define LOG_FATFS(...) do{ sddf_dprintf("FATFS|INFO: "); sddf_dprintf( __VA_ARGS__); } while(0)
#else
#define LOG_FATFS(...) do{}while(0)
#endif