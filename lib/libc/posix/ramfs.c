#include "ramfs.h"

long ramfs_fstatat(va_list ap);         // 0
long ramfs_readlinkat(va_list ap);      // 1
long ramfs_openat(va_list ap);          // 2
long ramfs_lseek(va_list ap);           // 3
long ramfs_mkdirat(va_list ap);         // 4
long ramfs_unlinkat(va_list ap);        // 5 
long ramfs_write(va_list ap);           // 6
long ramfs_read(va_list ap);            // 7
long ramfs_close(va_list ap);           // 8
long ramfs_dup3(va_list ap);            // 9
long ramfs_fstat(va_list ap);           // 10