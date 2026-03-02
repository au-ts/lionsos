/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/* Enable for component-local debug logging. */
/* #define EXT4_DEBUG_PRINT */

#define EXT4_FS_DATA_REGION_SIZE 0x4000000

#define EXT4_MAX_OPENED_FILENUM 32
#define EXT4_MAX_OPENED_DIRNUM 16

#define EXT4_WORKER_THREAD_NUM 1
#define EXT4_THREAD_NUM (EXT4_WORKER_THREAD_NUM + 1)
#define EXT4_WORKER_THREAD_STACKSIZE 0x40000

#define EXT4_BLOCK_DEVICE_NAME "blkdev0"
#define EXT4_MOUNT_POINT "/"
