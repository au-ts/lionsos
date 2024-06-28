#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FS_QUEUE_CAPACITY 511

#define FS_MAX_NAME_LENGTH 256
#define FS_MAX_PATH_LENGTH 4096

enum {
    FS_OPEN_FLAGS_READ_ONLY = 0,
    FS_OPEN_FLAGS_WRITE_ONLY = 1,
    FS_OPEN_FLAGS_READ_WRITE = 2,
    FS_OPEN_FLAGS_CREATE = 4,
};

enum {
    FS_STATUS_SUCCESS = 0,
    FS_STATUS_ERROR = 1,
};

enum {
    FS_CMD_OPEN,
    FS_CMD_CLOSE,
    FS_CMD_STAT,
    FS_CMD_FSTAT,
    FS_CMD_READ,
    FS_CMD_WRITE,
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

#define FS_NUM_COMMANDS 18

typedef struct fs_stat {
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
} fs_stat_t;

typedef struct fs_buffer {
    uint64_t offset;
    uint64_t size;
} fs_buffer_t;

typedef struct fs_cmd_params_open {
    fs_buffer_t path;
    uint64_t flags;
} fs_cmd_params_open_t;

typedef struct fs_cmd_params_close {
    uint64_t fd;
} fs_cmd_params_close_t;

typedef struct fs_cmd_params_stat {
    fs_buffer_t path;
    fs_buffer_t buf;
} fs_cmd_params_stat_t;

typedef struct fs_cmd_params_fstat {
    uint64_t fd;
    fs_buffer_t buf;
} fs_cmd_params_fstat_t;

typedef struct fs_cmd_params_read {
    uint64_t fd;
    uint64_t offset;
    fs_buffer_t buf;
} fs_cmd_params_read_t;

typedef struct fs_cmd_params_write {
    uint64_t fd;
    uint64_t offset;
    fs_buffer_t buf;
} fs_cmd_params_write_t;

typedef struct fs_cmd_params_rename {
    fs_buffer_t old_path;
    fs_buffer_t new_path;
} fs_cmd_params_rename_t;

typedef struct fs_cmd_params_unlink {
    fs_buffer_t path;
} fs_cmd_params_unlink_t;

typedef struct fs_cmd_params_truncate {
    uint64_t fd;
    uint64_t length;
} fs_cmd_params_truncate_t;

typedef struct fs_cmd_params_mkdir {
    fs_buffer_t path;
} fs_cmd_params_mkdir_t;

typedef struct fs_cmd_params_rmdir {
    fs_buffer_t path;
} fs_cmd_params_rmdir_t;

typedef struct fs_cmd_params_opendir {
    fs_buffer_t path;
} fs_cmd_params_opendir_t;

typedef struct fs_cmd_params_closedir {
    uint64_t fd;
} fs_cmd_params_closedir_t;

typedef struct fs_cmd_params_readdir {
    uint64_t fd;
    fs_buffer_t buf;
} fs_cmd_params_readdir_t;

typedef struct fs_cmd_params_fsync {
    uint64_t fd;
} fs_cmd_params_fsync_t;

typedef struct fs_cmd_params_seekdir {
    uint64_t fd;
    int64_t loc;
} fs_cmd_params_seekdir_t;

typedef struct fs_cmd_params_telldir {
    uint64_t fd;
} fs_cmd_params_telldir_t;

typedef struct fs_cmd_params_rewinddir {
    uint64_t fd;
} fs_cmd_params_rewinddir_t;

typedef union fs_cmd_params {
    fs_cmd_params_open_t open;
    fs_cmd_params_close_t close;
    fs_cmd_params_stat_t stat;
    fs_cmd_params_fstat_t fstat;
    fs_cmd_params_read_t read;
    fs_cmd_params_write_t write;
    fs_cmd_params_rename_t rename;
    fs_cmd_params_unlink_t unlink;
    fs_cmd_params_truncate_t truncate;
    fs_cmd_params_mkdir_t mkdir;
    fs_cmd_params_rmdir_t rmdir;
    fs_cmd_params_opendir_t opendir;
    fs_cmd_params_closedir_t closedir;
    fs_cmd_params_readdir_t readdir;
    fs_cmd_params_fsync_t fsync;
    fs_cmd_params_seekdir_t seekdir;
    fs_cmd_params_telldir_t telldir;
    fs_cmd_params_rewinddir_t rewinddir;

    char min_size[48];
} fs_cmd_params_t;

typedef struct fs_cmd {
    uint64_t id;
    uint64_t type;
    fs_cmd_params_t params;
} fs_cmd_t;

typedef struct fs_cmpl_data_open {
    uint64_t fd;
} fs_cmpl_data_open_t;

typedef struct fs_cmpl_data_read {
    uint64_t len_read;
} fs_cmpl_data_read_t;

typedef struct fs_cmpl_data_write {
    uint64_t len_written;
} fs_cmpl_data_write_t;

typedef struct fs_cmpl_data_opendir {
    uint64_t fd;
} fs_cmpl_data_opendir_t;

typedef struct fs_cmpl_data_readdir {
    uint64_t path_len;
} fs_cmpl_data_readdir_t;

typedef struct fs_cmpl_data_telldir {
    uint64_t location;
} fs_cmpl_data_telldir_t;

typedef union fs_cmpl_data {
    fs_cmpl_data_open_t open;
    fs_cmpl_data_read_t read;
    fs_cmpl_data_write_t write;
    fs_cmpl_data_opendir_t opendir;
    fs_cmpl_data_readdir_t readdir;
    fs_cmpl_data_telldir_t telldir;
} fs_cmpl_data_t;

typedef struct fs_cmpl {
    uint64_t id;
    uint64_t status;
    fs_cmpl_data_t data;
} fs_cmpl_t;

typedef union fs_msg {
    fs_cmd_t cmd;
    fs_cmpl_t cmpl;
} fs_msg_t;

_Static_assert(sizeof (fs_msg_t) == 64);

typedef struct fs_queue {
    uint64_t head;
    uint64_t tail;
    /* Add explicit padding to ensure buffer entries are cache-entry aligned. */
    uint64_t padding[6];
    fs_msg_t buffer[FS_QUEUE_CAPACITY];
} fs_queue_t;

static inline uint64_t fs_queue_size_consumer(fs_queue_t *queue) {
    return queue->tail - __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE);
}

static inline uint64_t fs_queue_size_producer(fs_queue_t *queue) {
    return __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE) - queue->head;
}

static inline fs_msg_t *fs_queue_idx_filled(fs_queue_t *queue, uint64_t index) {
    index = queue->head + index;
    return &queue->buffer[index % FS_QUEUE_CAPACITY];
}

static inline fs_msg_t *fs_queue_idx_empty(fs_queue_t *queue, uint64_t index) {
    index = queue->tail + index;
    return &queue->buffer[index % FS_QUEUE_CAPACITY];
}

static inline void fs_queue_publish_consumption(fs_queue_t *queue, uint64_t amount_consumed) {
    __atomic_store_n(&queue->head, queue->head + amount_consumed, __ATOMIC_RELEASE);
}

static inline void fs_queue_publish_production(fs_queue_t *queue, uint64_t amount_produced) {
    __atomic_store_n(&queue->tail, queue->tail + amount_produced, __ATOMIC_RELEASE);
}
