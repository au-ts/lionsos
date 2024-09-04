#pragma once

// Flag to control whether enable debug print for fatfs
// #define FS_DEBUG_PRINT

#define DATA_REGION_SIZE 0x4000000

// Maximum fat partition the file system can have
#define MAX_FATFS 1

// Maximum opened files
#define MAX_OPENED_FILENUM 32

// Maximum opened directories
#define MAX_OPENED_DIRNUM 16

// Actually this value should be like 4
#define BLK_QUEUE_SIZE 16

#define WORKER_COROUTINE_NUM 4

#define COROUTINE_NUM (WORKER_COROUTINE_NUM + 1)

#define LIBMICROKITCO_MAX_COTHREADS COROUTINE_NUM

#define COROUTINE_STACKSIZE 0x40000

#define BLK_DATA_ERGION_SIZE 0x200000

#define MAX_CLUSTER_SIZE (BLK_DATA_ERGION_SIZE / WORKER_COROUTINE_NUM)

/*
 *  This def control whether the memory address passed to the blk device driver should be strict aligned to the 
 *  BLK_TRANSFER_SIZE and if the BLK_TRANSFER_SIZE should be set to 4KB even if the file system is formatted with
 *  a different sector size. This setting is for future benchmarking purpose. When using with current SDDF, this should
 *  always be enabled even though it may cause some performance degradation.
 *  To enable benchmarking for zero copy, low overhead write, aside from delete the def below, the .system file and blk_config.h
 *  must be modified to correctly map shared memory between micropython and file system and fs_metadata to blk virt and blk driver.
 */
#define MEMBUF_STRICT_ALIGN_TO_BLK_TRANSFER_SIZE