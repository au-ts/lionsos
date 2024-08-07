#pragma once

#define DATA_REGION_SIZE 0x4000000

// Maximum fat partition the file system can have
#define MAX_FATFS 1

// Maximum opened files
#define MAX_OPENED_FILENUM 128

// Maximum opened directories
#define MAX_OPENED_DIRNUM 128

// Coroutine library that is using
#define USE_FIBERPOOL
// #define USE_LIBMICROKITCO

#define WORKER_COROUTINE_NUM 4

#define COROUTINE_STACKSIZE 0x40000