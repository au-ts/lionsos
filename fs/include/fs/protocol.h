#pragma once

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

struct sddf_fs_path {
    char str[4096];
    int len;
};

struct sddf_fs_filename {
    char str[255];
    int len;
};

struct sddf_fs_opendir_request {
    struct sddf_fs_path path;
};

struct sddf_fs_opendir_response {
    uint64_t fd;
    int status;
};

struct sddf_fs_readdir_request {
    uint64_t fd;
};

struct sddf_fs_readdir_response {
    struct sddf_fs_filename filename;
    int status;
};
