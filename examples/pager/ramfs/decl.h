#pragma once


#include <ramfs_config.h>
#include <lions/fs/protocol.h>

// io functions.
/**
 * libc -> IPC -> ramfs -> IPC -> process.
 */
long ramfs_fstatat();         // 0
long ramfs_readlinkat();      // 1
long ramfs_openat();          // 2
long ramfs_lseek();           // 3
long ramfs_mkdirat();         // 4
long ramfs_unlinkat();        // 5 
long ramfs_write();           // 6
long ramfs_read();            // 7
long ramfs_close();           // 8
long ramfs_dup3();            // 9
long ramfs_fstat();           // 10

// long ramfs_ioctl(va_list ap);
// long ramfs_fcntl(va_list ap);

// For debug
#ifdef RAMFS_DEBUG_PRINT
#include <sddf/util/printf.h>
#define LOG_RAMFS(...) do{ sddf_dprintf("RAMFS|INFO: "); sddf_dprintf( __VA_ARGS__); } while(0)
#else
#define LOG_RAMFS(...) do{}while(0)
#endif