#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FS_QUEUE_CAPACITY 511

enum {
    FS_OPEN_FLAGS_READ_ONLY = 0,
    FS_OPEN_FLAGS_WRITE_ONLY = 1,
    FS_OPEN_FLAGS_READ_WRITE = 2,
    FS_OPEN_FLAGS_CREATE = 4,
};

enum {
    FS_CMD_OPEN,
    FS_CMD_CLOSE,
    FS_CMD_STAT,
    FS_CMD_FSTAT,
    FS_CMD_PREAD,
    FS_CMD_PWRITE,
    FS_CMD_RENAME,
    FS_CMD_UNLINK,
    FS_CMD_TRUNCATE,
    FS_CMD_MKDIR,
    FS_CMD_RMDIR,
    FS_CMD_OPENDIR,
    FS_CMD_CLOSEDIR,
    FS_CMD_FSYNC,
    FS_CMD_READDIR,
    FS_CMD_SEEKDIR,
    FS_CMD_TELLDIR,
    FS_CMD_REWINDDIR,
};

struct fs_command {
    uint64_t request_id;
    uint64_t cmd_type;
    uint64_t args[6];
};

struct fs_completion {
    uint64_t request_id;
    uint64_t data[2];
    int32_t status;
};

union fs_message {
    struct fs_command command;
    struct fs_completion completion;
};

struct fs_queue {
    uint64_t head;
    uint64_t tail;
    /* Add explicit padding to ensure buffer entries are cache-entry aligned. */
    uint64_t padding[6];
    union fs_message buffer[FS_QUEUE_CAPACITY];
};

static bool fs_queue_push(struct fs_queue *queue, union fs_message message) {
    if (queue->tail - __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE) == FS_QUEUE_CAPACITY) {
        return false;
    }
    queue->buffer[queue->tail % FS_QUEUE_CAPACITY] = message;
    __atomic_store_n(&queue->tail, queue->tail + 1, __ATOMIC_RELEASE);
    return true;
}

static bool fs_queue_pop(struct fs_queue *queue, union fs_message *message) {
    if (queue->head == __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *message = queue->buffer[queue->head % FS_QUEUE_CAPACITY];
    __atomic_store_n(&queue->head, queue->head + 1, __ATOMIC_RELEASE);
    return true;
}

struct fs_stat_64 {
	uint64_t dev;
	uint64_t ino;
	uint64_t mode;
	uint64_t nlink;
	uint64_t uid;
	uint64_t gid;
	uint64_t rdev;
	uint64_t size;
	uint64_t blksize;
	uint64_t blocks;
	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint64_t atime_nsec;
	uint64_t mtime_nsec;
	uint64_t ctime_nsec;
	uint64_t used;
};
