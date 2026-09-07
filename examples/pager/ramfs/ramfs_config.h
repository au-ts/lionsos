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

#define RAMFS_BLOCK_SIZE 4096
#define RAMFS_TOTAL_BLOCKS (RAMFS_FS_DATA_REGION_SIZE / RAMFS_BLOCK_SIZE)
#define RAMFS_BITMAP_SIZE (RAMFS_TOTAL_BLOCKS / 8)
#define SLAB_SIZE 10000
#define FS_MAX_NAME_LENGTH 255
#define FS_MAX_PATH_LENGTH 4095