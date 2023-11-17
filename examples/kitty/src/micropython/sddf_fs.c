#include <microkit.h>
#include <string.h>
#include <fs/protocol.h>
#include "sddf_fs.h"
#include "micropython.h"

struct open_response sddf_fs_open(const char *filename)
{
    strcpy(nfs_share, filename);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_OPEN, 1));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    if (status) {
        return (struct open_response){ .status = status };
    } else {
        return (struct open_response){
            .status = status,
            .fd = *(uint64_t *)(nfs_share + 4)
        };
    }
}

int sddf_fs_close(uint64_t fd)
{
    microkit_mr_set(0, (uint64_t)fd);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_CLOSE, 1));
    await(mp_event_source_nfs);
    return *(int *)nfs_share;
}

struct read_response sddf_fs_pread(uint64_t fd, uint64_t nbyte, uint64_t offset) {
    microkit_mr_set(0, (uint64_t)fd);
    microkit_mr_set(1, nbyte);
    microkit_mr_set(2, offset);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_PREAD, 3));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    if (status < 0) {
        return (struct read_response){ .status = status };
    } else {
        return (struct read_response){
            .status = status,
            .data = nfs_share + 4,
            .len = status
        };
    }
}

int sddf_fs_pwrite(uint64_t fd, const char *buf, uint64_t nbyte, uint64_t offset) {
    memcpy(nfs_share, buf, nbyte);
    microkit_mr_set(0, (uint64_t)fd);
    microkit_mr_set(1, nbyte);
    microkit_mr_set(2, offset);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_PWRITE, 3));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

struct stat_response sddf_fs_stat(const char *filename)
{
    strcpy(nfs_share, filename);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_STAT, 0));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    if (status) {
        return (struct stat_response){ .status = status  };
    } else {
        struct stat_response response = { .status = status };
        memcpy(&response.stat, nfs_share + 4, sizeof (struct sddf_fs_stat_64));
        return response;
    }
}

int sddf_fs_rename(const char *oldpath, const char *newpath) {
    int oldpath_len = strlen(oldpath);
    int newpath_len = strlen(newpath);
    strcpy(nfs_share, oldpath);
    strcpy(nfs_share + oldpath_len + 1, newpath);
    microkit_mr_set(0, 0);
    microkit_mr_set(1, oldpath_len + 1);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_RENAME, 2));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_unlink(const char *path) {
    strcpy(nfs_share, path);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_UNLINK, 0));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_mkdir(const char *path) {
    strcpy(nfs_share, path);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_MKDIR, 0));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_rmdir(const char *path) {
    strcpy(nfs_share, path);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_RMDIR, 0));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_closedir(uint64_t fd) {
    microkit_mr_set(0, fd);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_CLOSEDIR, 1));

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_fsync(uint64_t fd) {
    microkit_mr_set(0, fd);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_FSYNC, 1));
    
    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

void sddf_fs_seekdir(uint64_t fd, long loc) {
    microkit_mr_set(0, fd);
    microkit_mr_set(1, loc);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_SEEKDIR, 2));
    
    await(mp_event_source_nfs);
}

long sddf_fs_telldir(uint64_t fd) {
    microkit_mr_set(0, fd);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_TELLDIR, 1));
    
    await(mp_event_source_nfs);

    long loc = *(long *)nfs_share;
    return loc;
}

void sddf_fs_rewinddir(uint64_t fd) {
    microkit_mr_set(0, fd);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_REWINDDIR, 1));
    
    await(mp_event_source_nfs);
}

int sddf_fs_opendir(const char *in_path, uint64_t *out_fd) {
    strcpy(nfs_share, in_path);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_OPENDIR, 0));

    await(mp_event_source_nfs);

    int status;
    memcpy(&status, nfs_share, 4);
    if (status == 0) {
        memcpy(out_fd, nfs_share + 4, 8);
    }
    return status;
}

int sddf_fs_readdir(uint64_t fd, char **out_name) {
    microkit_mr_set(0, fd);
    microkit_ppcall(NFS_CH, microkit_msginfo_new(SDDF_FS_CMD_READDIR, 1));

    await(mp_event_source_nfs);

    int status;
    memcpy(&status, nfs_share, 4);
    if (status == 0) {
        *out_name = nfs_share + 4;
    }
    return status;
}
