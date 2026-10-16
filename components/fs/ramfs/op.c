/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "decl.h"
#include "ff.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <lions/fs/protocol.h>
#include <lions/fs/server.h>
#include <fat_config.h>

/*
This file defines wrapper functions around FatFs that lib/libc/posix/ramfile.c calls directly, in the
client's own thread. Each returns an FS_STATUS_* value; out-parameters are only written on success.
*/

static FATFS fatfs;
static bool fs_initialised;

static FIL files[MAX_OPEN_FILES];
static bool file_used[MAX_OPEN_FILES];

static DIR dirs[MAX_OPEN_FILES];
static bool dir_used[MAX_OPEN_FILES];

static FIL *file_alloc(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_used[i]) {
            file_used[i] = true;
            return &files[i];
        }
    }
    return NULL;
}

static void file_free(FIL *file) {
    uint32_t i = file - files;
    assert(file_used[i]);
    file_used[i] = false;
}

static DIR *dir_alloc(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!dir_used[i]) {
            dir_used[i] = true;
            return &dirs[i];
        }
    }
    return NULL;
}

static void dir_free(DIR *dir) {
    uint32_t i = dir - dirs;
    assert(dir_used[i]);
    dir_used[i] = false;
}

// Function to convert the open flag from fs_protocol
static unsigned char map_fs_flags_to_fat_flags(uint64_t fs_flags) {
    unsigned char fat_flags = 0;

    // Map read/write flags
    switch (fs_flags & 0x3) {  // Mask to consider only the read/write bits
        case FS_OPEN_FLAGS_READ_ONLY:
            fat_flags |= FA_READ;
            break;
        case FS_OPEN_FLAGS_WRITE_ONLY:
            fat_flags |= FA_WRITE;
            break;
        case FS_OPEN_FLAGS_READ_WRITE:
            fat_flags |= (FA_READ | FA_WRITE);
            break;
    }

    // Map create flags
    if (fs_flags & FS_OPEN_FLAGS_CREATE) {
        // If the file exists, open the existing file. If not, create a new file.
        fat_flags |= FA_OPEN_ALWAYS;
    } else {
        fat_flags |= FA_OPEN_EXISTING;
    }

    return fat_flags;
}

// Change here later to support more than one FAT volumes
int handle_initialise(void) {
    LOG_FATFS("Mounting file system!\n");
    if (fs_initialised) {
        return FS_STATUS_ERROR;
    }
    fs_initialised = true;
    FRESULT RET = f_mount(&fatfs, "", 1);
    if (RET != FR_OK) {
        fs_initialised = false;
    }
    LOG_FATFS("Mounting file system result: %d\n", RET);
    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

int handle_deinitialise(void) {
    if (!fs_initialised) {
        return FS_STATUS_ERROR;
    }
    FRESULT RET = f_unmount("");
    if (RET == FR_OK) {
        fs_initialised = false;
    }
    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

int handle_file_open(const char *path, uint64_t openflag, fd_t *fd) {
    // Add open flag checking and mapping here
    LOG_FATFS("fat_open: file path: %s\n", path);

    FIL *file = file_alloc();
    if (file == NULL) {
        return FS_STATUS_TOO_MANY_OPEN_FILES;
    }

    unsigned char fat_flag = map_fs_flags_to_fat_flags(openflag);

    LOG_FATFS("fat_open: fs_protocol open flag: %lu\n", openflag);
    LOG_FATFS("fat_open: fat open flag: %hhu\n", fat_flag);

    // Micropython openflag still WIP, fixes this once that is completed
    FRESULT RET = f_open(file, path, fat_flag);

    // Error handling
    if (RET == FR_NO_FILE) {
        file_free(file);
        return FS_STATUS_NO_FILE;
    } else if (RET == FR_INVALID_NAME) {
        file_free(file);
        return FS_STATUS_INVALID_NAME;
    } else if (RET != FR_OK) {
        file_free(file);
        return FS_STATUS_ERROR;
    }

    int err = fd_alloc(fd);
    assert(!err);
    fd_set_file(*fd, file);

    return FS_STATUS_SUCCESS;
}

int handle_file_write(fd_t fd, const void *data, uint64_t len, uint64_t offset, uint64_t *len_written) {
    // f_write takes a 32-bit count, so writes larger than that come back short
    UINT btw = (len > UINT_MAX) ? UINT_MAX : len;

    LOG_FATFS("fat_write: bytes to be write: %u, write offset: %lu\n", btw, offset);

    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd: %d\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    FRESULT RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        fd_end_op(fd);
        return FS_STATUS_ERROR;
    }

    UINT bw = 0;

    RET = f_write(file, data, btw, &bw);
    fd_end_op(fd);

    if (RET == FR_OK) {
        LOG_FATFS("fat_write: byte written: %u, content written: \n%.*s\n", bw, bw, (char *)data);
    }
    else {
        LOG_FATFS("fat_write: error");
        return FS_STATUS_ERROR;
    }

    *len_written = bw;
    return FS_STATUS_SUCCESS;
}

int handle_file_read(fd_t fd, void *data, uint64_t len, uint64_t offset, uint64_t *len_read) {
    // f_read takes a 32-bit count, so reads larger than that come back short
    UINT btr = (len > UINT_MAX) ? UINT_MAX : len;

    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd: %d\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    LOG_FATFS("fat_read: bytes to be read: %u, read offset: %lu\n", btr, offset);

    FRESULT RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        fd_end_op(fd);
        return FS_STATUS_ERROR;
    }

    UINT br = 0;

    RET = f_read(file, data, btr, &br);
    fd_end_op(fd);

    if (RET == FR_OK) {
        LOG_FATFS("fat_read: byte read: %u, content read: \n%.*s\n", br, br, (char *)data);
    }
    else {
        LOG_FATFS("fat_read: error");
        return FS_STATUS_ERROR;
    }

    *len_read = br;
    return FS_STATUS_SUCCESS;
}

int handle_file_close(fd_t fd) {
    FIL *file;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("fat_close: Invalid file descriptor\n");
        return FS_STATUS_INVALID_FD;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        LOG_FATFS("fd has outstanding operations\n");
        return FS_STATUS_OUTSTANDING_OPERATIONS;
    }

    FRESULT RET = f_close(file);
    if (RET == FR_OK) {
        file_free(file);
        fd_free(fd);
    }
    else {
        fd_set_file(fd, file);
    }

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

int handle_stat(const char *path, fs_stat_t *file_stat) {
    LOG_FATFS("fat_stat:asking for filename: %s\n", path);

    FILINFO fileinfo;
    FRESULT RET = f_stat(path, &fileinfo);
    if (RET == FR_NO_FILE) {
        return FS_STATUS_NO_FILE;
    } else if (RET == FR_INVALID_NAME) {
        return FS_STATUS_INVALID_NAME;
    } else if (RET != FR_OK) {
        return FS_STATUS_ERROR;
    }

    LOG_FATFS("fat_stat result: size: %lu\n", (unsigned long)fileinfo.fsize);

    memset(file_stat, 0, sizeof(fs_stat_t));
    file_stat->atime = fileinfo.ftime;
    file_stat->ctime = fileinfo.ftime;
    file_stat->mtime = fileinfo.ftime;

    file_stat->size = fileinfo.fsize;

    // Now we have only one fat volume, so we can hard code it here
    file_stat->blksize = fatfs.ssize;

    // Everyone will have read, write and execute permissions by default
    // since this driver does not implement client checks and FAT itself
    // only stores a read-only bit with no concept of user/group/world
    file_stat->mode = 0777;

    if (fileinfo.fattrib & AM_DIR) {
        file_stat->mode |= S_IFDIR; // Assume regular directory
    } else {
        file_stat->mode |= S_IFREG; // Assume regular file
    }

    // Adjust for AM_RDO, if applicable
    if (fileinfo.fattrib & AM_RDO) {
        // If read-only and it's not a directory, remove write permissions.
        // Note: For directories, AM_RDO doesn't make sense to apply as "write"
        // because directories need to be writable for creating/removing files.
        if (!(fileinfo.fattrib & AM_DIR)) {
            file_stat->mode &= ~0222; // Remove write permissions
        }
    }

    return FS_STATUS_SUCCESS;
}

int handle_file_size(fd_t fd, uint64_t *size) {
    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd: %d\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    *size = f_size(file);
    fd_end_op(fd);
    return FS_STATUS_SUCCESS;
}

int handle_rename(const char *oldpath, const char *newpath) {
    FRESULT RET = f_rename(oldpath, newpath);

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

int handle_file_remove(const char *path) {
    FRESULT RET = f_unlink(path);

    if (RET == FR_NO_FILE) {
        return FS_STATUS_NO_FILE;
    } else if (RET != FR_OK) {
        return FS_STATUS_ERROR;
    } else {
        return FS_STATUS_SUCCESS;
    }
}

int handle_file_truncate(fd_t fd, uint64_t len) {
    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd");
        return FS_STATUS_INVALID_FD;
    }

    FRESULT RET = f_lseek(file, len);

    if (RET != FR_OK) {
        LOG_FATFS("fat_truncate: Invalid file offset\n");
        fd_end_op(fd);
        return FS_STATUS_ERROR;
    }

    RET = f_truncate(file);
    fd_end_op(fd);

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

int handle_dir_create(const char *path) {
    FRESULT RET = f_mkdir(path);
    if (RET == FR_OK) {
        return FS_STATUS_SUCCESS;
    } else if (RET == FR_EXIST) {
        return FS_STATUS_ALREADY_EXISTS;
    } else {
        return FS_STATUS_ERROR;
    }
}

// This seems to do the exact same thing as fat_unlink
int handle_dir_remove(const char *path) {
    FRESULT RET = f_rmdir(path);

    if (RET == FR_DENIED) {
        return FS_STATUS_NOT_EMPTY;
    } else if (RET != FR_OK) {
        return FS_STATUS_ERROR;
    } else {
        return FS_STATUS_SUCCESS;
    }
}

int handle_dir_open(const char *path, fd_t *fd) {
    DIR *dir = dir_alloc();
    if (dir == NULL) {
        return FS_STATUS_TOO_MANY_OPEN_FILES;
    }

    LOG_FATFS("FAT opendir directory path: %s\n", path);

    FRESULT RET = f_opendir(dir, path);

    // Error handling
    if (RET != FR_OK) {
        // Free this Dir structure
        dir_free(dir);
        if (RET == FR_NO_FILE || RET == FR_NO_PATH) {
            return FS_STATUS_NOT_DIRECTORY;
        } else {
            return FS_STATUS_ERROR;
        }
    }

    int err = fd_alloc(fd);
    assert(!err);
    fd_set_dir(*fd, dir);

    return FS_STATUS_SUCCESS;
}

int handle_dir_read(fd_t fd, char *name, uint64_t size, uint64_t *path_len) {
    LOG_FATFS("FAT readdir file descriptor: %lu\n", fd);

    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    FILINFO fno;
    FRESULT RET = f_readdir(dir, &fno);

    uint64_t len = strlen(fno.fname);
    // The buffer most have a size that is minimum length of the name plus one
    if (RET == FR_OK && size < len) {
        RET = FS_STATUS_ERROR;
    }

    if (RET == FR_OK) {
        *path_len = len;
        memcpy(name, fno.fname, len);
        LOG_FATFS("FAT readdir file name: %.*s\n", (uint32_t)len, (char*)name);
        // Hacky change the ret value to FS_STATUS_END_OF_DIRECTORY when nothing is in the directory
        if (fno.fname[0] == 0) {
            RET = FS_STATUS_END_OF_DIRECTORY;
        }
    }

    fd_end_op(fd);

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// Not sure if this one is implemented correctly
int handle_dir_tell(fd_t fd, uint64_t *location) {
    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    *location = f_telldir(dir);
    fd_end_op(fd);

    return FS_STATUS_SUCCESS;
}

int handle_dir_rewind(fd_t fd) {
    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    FRESULT RET = f_readdir(dir, 0);
    fd_end_op(fd);

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

int handle_file_sync(fd_t fd) {
    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    FRESULT RET = f_sync(file);
    fd_end_op(fd);

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

int handle_dir_close(fd_t fd) {
    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        return FS_STATUS_INVALID_FD;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        LOG_FATFS("trying to close fd with outstanding operations\n");
        return FS_STATUS_OUTSTANDING_OPERATIONS;
    }

    FRESULT RET = f_closedir(dir);

    if (RET == FR_OK) {
        fd_free(fd);
        dir_free(dir);
    }

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// Inefficient implementation of seekdir
// There is no function as seekdir in the current Fatfs library
// I can add one to the library but I do not want to add another layer of instability
// So just use this inefficient one for now
int handle_dir_seek(fd_t fd, int64_t loc) {
    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        return FS_STATUS_INVALID_FD;
    }

    FRESULT RET = f_readdir(dir, 0);
    FILINFO fno;

    for (int64_t i = 0; i < loc; i++) {
        if (RET != FR_OK) {
            fd_end_op(fd);
            return FS_STATUS_ERROR;
        }
        RET = f_readdir(dir, &fno);
    }
    fd_end_op(fd);

    return (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}
