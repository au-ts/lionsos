#include <microkit.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <fs/protocol.h>
#include "sddf_fs.h"
#include "micropython.h"

#define NFS_SHARE_BUF_SIZE 4096

struct sddf_fs_queue *nfs_command_queue;
struct sddf_fs_queue *nfs_completion_queue;

uint64_t curr_request_id = 0;

uint64_t nfs_share_buf_offset[] = {
    NFS_SHARE_BUF_SIZE,
    NFS_SHARE_BUF_SIZE * 2,
    NFS_SHARE_BUF_SIZE * 3,
    NFS_SHARE_BUF_SIZE * 4
};

char *nfs_share_buf(int n) {
    return nfs_share + nfs_share_buf_offset[n];
}

struct sddf_fs_completion cmd_await(uint32_t cmd_type, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    union sddf_fs_message message;

    message.command = (struct sddf_fs_command) {
        .request_id = curr_request_id,
        .cmd_type = cmd_type,
        .args = {
            [0] = arg0,
            [1] = arg1,
            [2] = arg2,
            [3] = arg3,
        }
    };
    bool success = sddf_fs_queue_push(nfs_command_queue, message);
    assert(success);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    success = sddf_fs_queue_pop(nfs_completion_queue, &message);
    assert(success);
    assert(message.completion.request_id == curr_request_id);
    curr_request_id++;
    return message.completion;
}

struct open_response sddf_fs_open(const char *path)
{
    strcpy(nfs_share_buf(0), path);
    uint64_t path_len = strlen(path) + 1;
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_OPEN, nfs_share_buf_offset[0], path_len, 0, 0);

    struct open_response response;
    response.status = cmpl.status;
    if (cmpl.status == 0) {
        response.fd = cmpl.data[0];
    }
    return response;
}

int sddf_fs_close(uint64_t fd)
{
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_CLOSE, fd, 0, 0, 0);
    return cmpl.status;
}

struct read_response sddf_fs_pread(uint64_t fd, uint64_t nbyte, uint64_t offset) {
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_PREAD, fd, nfs_share_buf_offset[0], nbyte, offset);

    struct read_response response;
    response.status = cmpl.status;
    if (cmpl.status >= 0) {
        response.data = nfs_share_buf(0);
        response.len = cmpl.status;
    }
    return response;
}

int sddf_fs_pwrite(uint64_t fd, const char *buf, uint64_t nbyte, uint64_t offset) {
    memcpy(nfs_share_buf(0), buf, nbyte);
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_PWRITE, fd, nfs_share_buf_offset[0], nbyte, offset);
    return cmpl.status;
}

struct stat_response sddf_fs_stat(const char *filename) {
    uint64_t path_len = strlen(filename) + 1;
    strcpy(nfs_share_buf(0), filename);
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_STAT, nfs_share_buf_offset[0], path_len,nfs_share_buf_offset[1], 0);

    struct stat_response response;
    response.status = cmpl.status;
    if (cmpl.status == 0) {
        memcpy(&response.stat, nfs_share_buf(1), sizeof (struct sddf_fs_stat_64));
    }
    return response;
}

int sddf_fs_rename(const char *oldpath, const char *newpath) {
    uint64_t oldpath_len = strlen(oldpath) + 1;
    uint64_t newpath_len = strlen(newpath) + 1;
    strcpy(nfs_share_buf(0), oldpath);
    strcpy(nfs_share_buf(1), newpath);
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_RENAME, nfs_share_buf_offset[0], oldpath_len, nfs_share_buf_offset[1], newpath_len);
    return cmpl.status;
}

int sddf_fs_unlink(const char *path) {
    uint64_t path_len = strlen(path) + 1;
    strcpy(nfs_share_buf(0), path);
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_UNLINK, nfs_share_buf_offset[0], path_len, 0, 0);
    return cmpl.status;
}

int sddf_fs_mkdir(const char *path) {
    uint64_t path_len = strlen(path) + 1;
    strcpy(nfs_share_buf(0), path);
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_MKDIR, nfs_share_buf_offset[0], path_len, 0, 0);
    return cmpl.status;
}

int sddf_fs_rmdir(const char *path) {
    uint64_t path_len = strlen(path) + 1;
    strcpy(nfs_share_buf(0), path);
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_RMDIR, nfs_share_buf_offset[0], path_len, 0, 0);
    return cmpl.status;
}

int sddf_fs_closedir(uint64_t fd) {
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_CLOSEDIR, fd, 0, 0, 0);
    return cmpl.status;
}

int sddf_fs_fsync(uint64_t fd) {
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_FSYNC, fd, 0, 0, 0);
    return cmpl.status;
}

void sddf_fs_seekdir(uint64_t fd, long loc) {
    cmd_await(SDDF_FS_CMD_SEEKDIR, fd, loc, 0, 0);
}

long sddf_fs_telldir(uint64_t fd) {
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_TELLDIR, fd, 0, 0, 0);
    return cmpl.status ? cmpl.status : cmpl.data[0];
}

void sddf_fs_rewinddir(uint64_t fd) {
    cmd_await(SDDF_FS_CMD_REWINDDIR, fd, 0, 0, 0);
}

int sddf_fs_opendir(const char *path, uint64_t *fd) {
    uint64_t path_len = strlen(path) + 1;
    strcpy(nfs_share_buf(0), path);
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_OPENDIR, nfs_share_buf_offset[0], path_len, 0, 0);

    int status = cmpl.status;
    if (status == 0) {
        *fd = cmpl.data[0];
    }
    return status;
}

int sddf_fs_readdir(uint64_t fd, char **out_name) {
    struct sddf_fs_completion cmpl = cmd_await(SDDF_FS_CMD_READDIR, fd, nfs_share_buf_offset[0], NFS_SHARE_BUF_SIZE, 0);

    int status = cmpl.status;
    if (status == 0) {
        *out_name = nfs_share_buf(0);
    }
    return status;
}
