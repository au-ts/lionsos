/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "decl.h"
#include "ff.h"
#include <libmicrokitco.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <lions/fs/protocol.h>
#include <lions/fs/server.h>
#include <fat_config.h>

/*
This file define a bunch of wrapper functions of FATFs functions so those functions can be run in the
worker thread.
*/

FATFS fatfs;
bool fs_initialised;

FIL files[MAX_OPEN_FILES];
bool file_used[MAX_OPEN_FILES];

DIR dirs[MAX_OPEN_FILES];
bool dir_used[MAX_OPEN_FILES];

/* Data shared with client */
extern char *fs_share;

FIL *file_alloc(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_used[i]) {
            file_used[i] = true;
            return &files[i];
        }
    }
    return NULL;
}

void file_free(FIL *file) {
    uint32_t i = file - files;
    assert(file_used[i]);
    file_used[i] = false;
}

DIR *dir_alloc(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!dir_used[i]) {
            dir_used[i] = true;
            return &dirs[i];
        }
    }
    return NULL;
}

void dir_free(DIR *dir) {
    uint32_t i = dir - dirs;
    assert(dir_used[i]);
    dir_used[i] = false;
}

// Function to convert the open flag from fs_protocol
unsigned char map_fs_flags_to_fat_flags(uint64_t fs_flags) {
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
void handle_initialise(void) {
    LOG_FATFS("Mounting file system!\n");
    co_data_t *args = microkit_cothread_my_arg();
    if (fs_initialised) {
        args->status = FS_STATUS_ERROR;
        return;
    }
    fs_initialised = true;
    FRESULT RET = f_mount(&fatfs, "", 1);
    if (RET != FR_OK) {
        fs_initialised = false;
    }
    LOG_FATFS("Mounting file system result: %d\n", RET);
    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_deinitialise(void) {
    co_data_t *args = microkit_cothread_my_arg();
    if (!fs_initialised) {
        args->status = FS_STATUS_ERROR;
        return;
    }
    FRESULT RET = f_unmount("");
    if (RET == FR_OK) {
        fs_initialised = false;
    }
    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_open(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.file_open.path;
    uint64_t openflag = args->params.file_open.flags;

    // Copy the name to our name buffer
    char filepath[FS_MAX_NAME_LENGTH + 1];

    int err = fs_copy_client_path(filepath, fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (err) {
        args->status = FS_STATUS_ERROR;
        return;
    }

    // Add open flag checking and mapping here
    LOG_FATFS("fat_open: file path: %s\n", filepath);

    FIL *file = file_alloc();
    if (file == NULL) {
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    unsigned char fat_flag = map_fs_flags_to_fat_flags(openflag);

    LOG_FATFS("fat_open: fs_protocol open flag: %lu\n", openflag);
    LOG_FATFS("fat_open: fat open flag: %hhu\n", fat_flag);

    // Micropython openflag still WIP, fixes this once that is completed
    FRESULT RET = f_open(file, filepath, fat_flag);

    // Error handling
    if (RET != FR_OK) {
        file_free(file);
        args->status = FS_STATUS_ERROR;
        return;
    }

    fd_t fd;
    err = fd_alloc(&fd);
    assert(!err);
    fd_set_file(fd, file);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
    args->result.file_open.fd = fd;
}

void handle_file_write(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_write.fd;
    // uint64_t buffer = args->params.file_write.buf.offset;
    fs_buffer_t buffer = args->params.file_write.buf;
    uint64_t btw = args->params.file_write.buf.size;
    uint64_t offset = args->params.file_write.offset;

    LOG_FATFS("fat_write: bytes to be write: %lu, write offset: %lu\n", btw, offset);

    char *data = fs_get_client_buffer(fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (data == NULL) {
        LOG_FATFS("fat_write: invalid buffer\n");
        args->result.file_write.len_written = 0;
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd: %d\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    FRESULT RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        fd_end_op(fd);
        args->result.file_write.len_written = 0;
        args->status = FS_STATUS_ERROR;
        return;
    }

    uint32_t bw = 0;

    RET = f_write(file, data, btw, &bw);
    fd_end_op(fd);

    if (RET == FR_OK) {
        LOG_FATFS("fat_write: byte written: %u, content written: \n%.*s\n", bw, bw, (char *)data);
    }
    else {
        LOG_FATFS("fat_write: error");
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
    args->result.file_write.len_written = bw;
}

void handle_file_read(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_read.fd;
    fs_buffer_t buffer = args->params.file_read.buf;
    uint64_t btr = args->params.file_read.buf.size;
    uint64_t offset = args->params.file_read.offset;

    char *data = fs_get_client_buffer(fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (data == NULL) {
        LOG_FATFS("fat_read: invalid buffer provided\n");
        args->status = FS_STATUS_INVALID_BUFFER;
        args->result.file_read.len_read = 0;
        return;
    }

    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd: %d\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    LOG_FATFS("fat_read: bytes to be read: %lu, read offset: %lu\n", btr, offset);

    FRESULT RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        fd_end_op(fd);
        args->status = FS_STATUS_ERROR;
        args->result.file_read.len_read = 0;
        return;
    }

    uint32_t br = 0;

    RET = f_read(file, data, btr, &br);
    fd_end_op(fd);

    if (RET == FR_OK) {
        LOG_FATFS("fat_read: byte read: %u, content read: \n%.*s\n", br, br, (char *)data);
    }
    else {
        LOG_FATFS("fat_read: error");
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
    args->result.file_read.len_read = br;
}

void handle_file_close(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_close.fd;

    FIL *file;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("fat_close: Invalid file descriptor\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        LOG_FATFS("fd has outstanding operations\n");
        args->status = FS_STATUS_OUTSTANDING_OPERATIONS;
        return;
    }

    FRESULT RET = f_close(file);
    if (RET == FR_OK) {
        file_free(file);
        fd_free(fd);
    }
    else {
        fd_set_file(fd, file);
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_stat(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t path = args->params.stat.path;
    fs_buffer_t output_buffer = args->params.stat.buf;
    uint64_t size = args->params.stat.buf.size;

    char filepath[FS_MAX_PATH_LENGTH + 1];

    fs_stat_t *file_stat = fs_get_client_buffer(fs_share, FAT_FS_DATA_REGION_SIZE, output_buffer);
    if (file_stat == NULL || size < sizeof (fs_stat_t)) {
        LOG_FATFS("invalid output buffer provided\n");
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    int err = fs_copy_client_path(filepath, fs_share, FAT_FS_DATA_REGION_SIZE, path);
    if (err) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    LOG_FATFS("fat_stat:asking for filename: %s\n", filepath);

    FILINFO fileinfo;
    FRESULT RET = f_stat(filepath, &fileinfo);
    if (RET == FR_NO_FILE) {
        args->status = FS_STATUS_NO_FILE;
        return;
    } else if (RET == FR_INVALID_NAME) {
        args->status = FS_STATUS_INVALID_NAME;
        return;
    }else if (RET != FR_OK) {
        LOG_FATFS("fat_stat: RET = %d\n", RET);
        args->status = FS_STATUS_ERROR;
        return;
    }

    memset(file_stat, 0, sizeof(fs_stat_t));
    file_stat->atime = fileinfo.ftime;
    file_stat->ctime = fileinfo.ftime;
    file_stat->mtime = fileinfo.ftime;

    file_stat->size = fileinfo.fsize;

// Now we have only one fat volume, so we can hard code it here
    file_stat->blksize = fatfs.ssize;

// Study how is the structure of the mode, just leave it for now
    file_stat->mode = 0;
    if (fileinfo.fattrib & AM_DIR) {
        file_stat->mode |= 040755; // Directory with rwx for owner, rx for group and others
    } else {
        // Assume regular file, apply read-only attribute
        file_stat->mode |= 0444; // Readable by everyone
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

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_size(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.file_size.fd;

    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd: %d\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    uint64_t size = f_size(file);
    fd_end_op(fd);
    args->status = FS_STATUS_SUCCESS;
    args->result.file_size.size = size;
}

void handle_rename(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t oldpath_buffer = args->params.rename.old_path;
    fs_buffer_t newpath_buffer = args->params.rename.new_path;

    char oldpath[FS_MAX_PATH_LENGTH + 1];
    char newpath[FS_MAX_PATH_LENGTH + 1];

    int err = fs_copy_client_path(oldpath, fs_share, FAT_FS_DATA_REGION_SIZE, oldpath_buffer);
    if (err) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }
    err = fs_copy_client_path(newpath, fs_share, FAT_FS_DATA_REGION_SIZE, newpath_buffer);
    if (err) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    FRESULT RET = f_rename(oldpath, newpath);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_remove(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.file_remove.path;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    int err = fs_copy_client_path(dirpath, fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (err) {
        LOG_FATFS("fat_unlink: invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    FRESULT RET = f_unlink(dirpath);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_truncate(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.file_truncate.fd;
    uint64_t len = args->params.file_truncate.length;

    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    FRESULT RET = f_lseek(file, len);

    if (RET != FR_OK) {
        LOG_FATFS("fat_truncate: Invalid file offset\n");
        fd_end_op(fd);
        args->status = FS_STATUS_ERROR;
        return;
    }

    RET = f_truncate(file);
    fd_end_op(fd);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_dir_create(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.dir_create.path;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    int err = fs_copy_client_path(dirpath, fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (err) {
        LOG_FATFS("fat_mkdir: Invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    FRESULT RET = f_mkdir(dirpath);
    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// This seems to do the exact same thing as fat_unlink
void handle_dir_remove(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.dir_remove.path;

    char dirpath[FS_MAX_PATH_LENGTH + 1];

    int err = fs_copy_client_path(dirpath, fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (err) {
        LOG_FATFS("fat_mkdir: Invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    FRESULT RET = f_rmdir(dirpath);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_dir_open(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fs_buffer_t buffer = args->params.dir_open.path;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    int err = fs_copy_client_path(dirpath, fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (err) {
        LOG_FATFS("fat_readdir: Invalid buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    DIR *dir = dir_alloc();
    if (dir == NULL) {
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    LOG_FATFS("FAT opendir directory path: %s\n", dirpath);

    FRESULT RET = f_opendir(dir, dirpath);

    // Error handling
    if (RET != FR_OK) {
        LOG_FATFS("FRESULT: %d\n", RET);
        args->status = FS_STATUS_ERROR;
        // Free this Dir structure
        dir_free(dir);
        return;
    }

    fd_t fd;
    err = fd_alloc(&fd);
    assert(!err);
    fd_set_dir(fd, dir);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
    args->result.dir_open.fd = fd;
}

void handle_dir_read(void) {
    co_data_t *args = microkit_cothread_my_arg();

    // Dir descriptor
    fd_t fd = args->params.dir_read.fd;
    fs_buffer_t buffer = args->params.dir_read.buf;
    uint64_t size = args->params.dir_read.buf.size;

    LOG_FATFS("FAT readdir file descriptor: %lu\n", fd);

    char *name = fs_get_client_buffer(fs_share, FAT_FS_DATA_REGION_SIZE, buffer);
    if (name == NULL) {
        LOG_FATFS("fat_readdir: invalid buffer\n");
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }

    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    FILINFO fno;
    FRESULT RET = f_readdir(dir, &fno);

    uint64_t len = strlen(fno.fname);
    // The buffer most have a size that is minimum length of the name plus one
    if (RET == FR_OK && size < len) {
        RET = FS_STATUS_ERROR;
    }

    if (RET == FR_OK) {
        args->result.dir_read.path_len = len;
        memcpy(name, fno.fname, len);
        LOG_FATFS("FAT readdir file name: %.*s\n", (uint32_t)len, (char*)name);
        // Hacky change the ret value to FS_STATUS_END_OF_DIRECTORY when nothing is in the directory
        if (fno.fname[0] == 0) {
            RET = FS_STATUS_END_OF_DIRECTORY;
        }
    }

    fd_end_op(fd);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// Not sure if this one is implemented correctly
void handle_dir_tell(void){
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.dir_tell.fd;

    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    uint32_t offset = f_telldir(dir);
    fd_end_op(fd);

    args->status = FS_STATUS_SUCCESS;
    args->result.dir_tell.location = offset;
}

void handle_dir_rewind(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.dir_rewind.fd;

    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    FRESULT RET = f_readdir(dir, 0);
    fd_end_op(fd);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_sync(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fd_t fd = args->params.file_sync.fd;

    FIL *file = NULL;
    int err = fd_begin_op_file(fd, (void **)&file);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    FRESULT RET = f_sync(file);
    fd_end_op(fd);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_dir_close(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.dir_close.fd;

    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }
    fd_end_op(fd);

    err = fd_unset(fd);
    if (err) {
        LOG_FATFS("trying to close fd with outstanding operations\n");
        args->status = FS_STATUS_OUTSTANDING_OPERATIONS;
        return;
    }

    FRESULT RET = f_closedir(dir);

    if (RET == FR_OK) {
        fd_free(fd);
        dir_free(dir);
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// Inefficient implementation of seekdir
// There is no function as seekdir in the current Fatfs library
// I can add one to the library but I do not want to add another layer of instability
// So just use this inefficient one for now
void handle_dir_seek(void) {
    co_data_t *args = microkit_cothread_my_arg();

    fd_t fd = args->params.dir_seek.fd;
    int64_t loc = args->params.dir_seek.loc;

    DIR *dir = NULL;
    int err = fd_begin_op_dir(fd, (void **)&dir);
    if (err) {
        LOG_FATFS("invalid fd (%d)\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    FRESULT RET = f_readdir(dir, 0);
    FILINFO fno;

    for (int64_t i = 0; i < loc; i++) {
        if (RET != FR_OK) {
            args->status = FS_STATUS_ERROR;
            fd_end_op(fd);
            return;
        }
        RET = f_readdir(dir, &fno);
    }
    fd_end_op(fd);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}
