#include <microkit.h>
#include <string.h>
#include <fs/protocol.h>
#include "sddf_fs.h"
#include "micropython.h"

struct open_response sddf_fs_open(const char *filename)
{
    uint32_t cmd = SDDF_FS_CMD_OPEN;
    memcpy(nfs_share, &cmd, 4);
    strcpy(nfs_share + 4, filename);
    microkit_notify(NFS_CH);

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
    uint32_t cmd = SDDF_FS_CMD_CLOSE;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    return *(int *)nfs_share;
}

struct read_response sddf_fs_pread(uint64_t fd, uint64_t nbyte, uint64_t offset) {
    uint32_t cmd = SDDF_FS_CMD_PREAD;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    memcpy(nfs_share + 12, &nbyte, 8);
    memcpy(nfs_share + 20, &offset, 8);
    microkit_notify(NFS_CH);

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
    uint32_t cmd = SDDF_FS_CMD_PWRITE;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    memcpy(nfs_share + 12, &nbyte, 8);
    memcpy(nfs_share + 20, &offset, 8);
    memcpy(nfs_share + 28, buf, nbyte);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

struct stat_response sddf_fs_stat(const char *filename) {
    uint32_t cmd = SDDF_FS_CMD_STAT;
    memcpy(nfs_share, &cmd, 4);
    strcpy(nfs_share + 4, filename);
    microkit_notify(NFS_CH);

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
    uint32_t cmd = SDDF_FS_CMD_RENAME;
    uint64_t oldpath_offset = 20;
    uint64_t newpath_offset = oldpath_offset + strlen(oldpath) + 1;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &oldpath_offset, 8);
    memcpy(nfs_share + 12, &newpath_offset, 8);
    strcpy(nfs_share + oldpath_offset, oldpath);
    strcpy(nfs_share + newpath_offset, newpath);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_unlink(const char *path) {
    uint32_t cmd = SDDF_FS_CMD_UNLINK;
    memcpy(nfs_share, &cmd, 4);
    strcpy(nfs_share + 4, path);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_mkdir(const char *path) {
    uint32_t cmd = SDDF_FS_CMD_MKDIR;
    memcpy(nfs_share, &cmd, 4);
    strcpy(nfs_share + 4, path);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_rmdir(const char *path) {
    uint32_t cmd = SDDF_FS_CMD_RMDIR;
    memcpy(nfs_share, &cmd, 4);
    strcpy(nfs_share + 4, path);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_closedir(uint64_t fd) {
    uint32_t cmd = SDDF_FS_CMD_CLOSEDIR;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

int sddf_fs_fsync(uint64_t fd) {
    uint32_t cmd = SDDF_FS_CMD_FSYNC;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    microkit_notify(NFS_CH);
    
    await(mp_event_source_nfs);

    int status = *(int *)nfs_share;
    return status;
}

void sddf_fs_seekdir(uint64_t fd, long loc) {
    uint32_t cmd = SDDF_FS_CMD_CLOSEDIR;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    memcpy(nfs_share + 12, &loc, 8);
    microkit_notify(NFS_CH);
    
    await(mp_event_source_nfs);
}

long sddf_fs_telldir(uint64_t fd) {
    uint32_t cmd = SDDF_FS_CMD_TELLDIR;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    microkit_notify(NFS_CH);
    
    await(mp_event_source_nfs);

    long loc = *(long *)nfs_share;
    return loc;
}

void sddf_fs_rewinddir(uint64_t fd) {
    uint32_t cmd = SDDF_FS_CMD_REWINDDIR;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    microkit_notify(NFS_CH);
    
    await(mp_event_source_nfs);
}

int sddf_fs_opendir(const char *in_path, uint64_t *out_fd) {
    uint32_t cmd = SDDF_FS_CMD_OPENDIR;
    memcpy(nfs_share, &cmd, 4);
    strcpy(nfs_share + 4, in_path);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status;
    memcpy(&status, nfs_share, 4);
    if (status == 0) {
        memcpy(out_fd, nfs_share + 4, 8);
    }
    return status;
}

int sddf_fs_readdir(uint64_t fd, char **out_name) {
    uint32_t cmd = SDDF_FS_CMD_READDIR;
    memcpy(nfs_share, &cmd, 4);
    memcpy(nfs_share + 4, &fd, 8);
    microkit_notify(NFS_CH);

    await(mp_event_source_nfs);

    int status;
    memcpy(&status, nfs_share, 4);
    if (status == 0) {
        *out_name = nfs_share + 4;
    }
    return status;
}
