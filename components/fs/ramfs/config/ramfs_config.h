#pragma once


// Flag to control whether enabling debug printing
// #define RAMFS_DEBUG_PRINT


// The size of the ramfs memory region
#define RAMFS_FS_DATA_REGION_SIZE 0x20000000 // 512MB


// Path to the CPIO archive to initialize the ramfs from
#define RAMFS_CPIO_PATH "initramfs.cpio.gz"


// Maximum opened files
#define RAMFS_MAX_OPENED_FILENUM 32


// Maximum opened directories
#define RAMFS_MAX_OPENED_DIRNUM 16


// Hash table size for O(1) lookups
#define RAMFS_HASH_TABLE_SIZE 256
