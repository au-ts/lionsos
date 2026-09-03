#pragma once


#include <ramfs_config.h>
#include <lions/fs/protocol.h>


// Use struct instead of union
typedef struct {
    fs_cmd_params_t params;
    uint64_t status;
    fs_cmpl_data_t result;
} co_data_t;

ssize_t ramfs_write(const void *buf, size_t len, int fd);
ssize_t ramfs_read(void *buf, size_t len, int fd);
int ramfs_close(int fd);
int ramfs_dup3(int oldfd, int newfd);
int ramfs_fstat(int fd, struct stat *statbuf);

// For debug
#ifdef RAMFS_DEBUG_PRINT
#include <sddf/util/printf.h>
#define LOG_RAMFS(...) do{ sddf_dprintf("RAMFS|INFO: "); sddf_dprintf( __VA_ARGS__); } while(0)
#else
#define LOG_RAMFS(...) do{}while(0)
#endif
