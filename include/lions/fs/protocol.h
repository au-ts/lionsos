#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SDDF_FS_QUEUE_CAPACITY 511

enum {
    SDDF_FS_CMD_OPEN,
    SDDF_FS_CMD_CLOSE,
    SDDF_FS_CMD_STAT,
    SDDF_FS_CMD_PREAD,
    SDDF_FS_CMD_PWRITE,
    SDDF_FS_CMD_RENAME,
    SDDF_FS_CMD_UNLINK,
    SDDF_FS_CMD_MKDIR,
    SDDF_FS_CMD_RMDIR,
    SDDF_FS_CMD_OPENDIR,
    SDDF_FS_CMD_CLOSEDIR,
    SDDF_FS_CMD_FSYNC,
    SDDF_FS_CMD_READDIR,
    SDDF_FS_CMD_SEEKDIR,
    SDDF_FS_CMD_TELLDIR,
    SDDF_FS_CMD_REWINDDIR,
};

struct sddf_fs_command {
    uint64_t request_id;
    uint64_t cmd_type;
    uint64_t args[6];
};

struct sddf_fs_completion {
    uint64_t request_id;
    uint64_t data[2];
    int32_t status;
};

union sddf_fs_message {
    struct sddf_fs_command command;
    struct sddf_fs_completion completion;
};

struct sddf_fs_queue {
    uint64_t head;
    uint64_t tail;
    /* Add explicit padding to ensure buffer entries are cache-entry aligned. */
    uint64_t padding[6];
    union sddf_fs_message buffer[SDDF_FS_QUEUE_CAPACITY];
};

static bool sddf_fs_queue_push(struct sddf_fs_queue *queue, union sddf_fs_message message) {
    if (queue->tail + 1 == __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE)) {
        return false;
    }
    queue->buffer[queue->tail % SDDF_FS_QUEUE_CAPACITY] = message;
    __atomic_store_n(&queue->tail, queue->tail + 1, __ATOMIC_RELEASE);
    return true;
}

static bool sddf_fs_queue_pop(struct sddf_fs_queue *queue, union sddf_fs_message *message) {
    if (queue->head == __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *message = queue->buffer[queue->head % SDDF_FS_QUEUE_CAPACITY];
    __atomic_store_n(&queue->head, queue->head + 1, __ATOMIC_RELEASE);
    return true;
}

struct sddf_fs_stat_64 {
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
