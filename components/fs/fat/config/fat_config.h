/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Flag to control whether enabling debug printing
#define FAT_DEBUG_PRINT

#define FAT_FS_DATA_REGION_SIZE 0x4000000

// Maximum opened files
#define FAT_MAX_OPENED_FILENUM 32

// Maximum opened directories
#define FAT_MAX_OPENED_DIRNUM 16

// The number of worker thread, if changes are needed, initialisation of the thread pool must be changes as well
#define FAT_WORKER_THREAD_NUM 4

#define FAT_THREAD_NUM (FAT_WORKER_THREAD_NUM + 1)

#define FAT_WORKER_THREAD_STACKSIZE 0x40000
