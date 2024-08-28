#pragma once

// Flag to control whether enable debug print for fatfs
#define FS_DEBUG_PRINT

#define DATA_REGION_SIZE 0x4000000

// Maximum fat partition the file system can have
#define MAX_FATFS 1

// Maximum opened files
#define MAX_OPENED_FILENUM 128

// Maximum opened directories
#define MAX_OPENED_DIRNUM 128

// Actually this value should be like 4
#define BLK_QUEUE_SIZE 16

// Coroutine library that is using
#ifndef USE_FIBERPOOL
#ifndef USE_LIBMICROKITCO
#define USE_LIBMICROKITCO
#endif
#endif

#define WORKER_COROUTINE_NUM 4

#define COROUTINE_NUM (WORKER_COROUTINE_NUM + 1)

#define LIBMICROKITCO_MAX_COTHREADS COROUTINE_NUM

#define COROUTINE_STACKSIZE 0x40000