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
#include <lions/fs/protocol.h>
#include <fat_config.h>

/*
This file define a bunch of wrapper functions of FATFs functions so those functions can be run in the
worker thread.
*/

typedef enum : uint8_t {
    FREE = 0,
    INUSE = 1,
    CLEANUP = 2,
} descriptor_status;

descriptor_status fs_status;
FATFS fatfs;
descriptor_status file_status[FAT_MAX_OPENED_FILENUM];
FIL files[FAT_MAX_OPENED_FILENUM];
descriptor_status dir_status[FAT_MAX_OPENED_DIRNUM];
DIR dirs[FAT_MAX_OPENED_DIRNUM];

/* Data shared with client */
extern char *fs_share;

// Sanity check functions
// Checking if the memory region that provided by request is within valid memory region
static inline FRESULT within_data_region(uint64_t offset, uint64_t buffer_size) {
    LOG_FATFS("with_data_region check, input args: offset: %ld, buffer size: %ld\n", offset, buffer_size);
    if ((offset < FAT_FS_DATA_REGION_SIZE) && (buffer_size <= FAT_FS_DATA_REGION_SIZE - offset)) {
        return FR_OK;
    }
    return FR_INVALID_PARAMETER;
}

// Checking if the descriptor is mapped to a valid object
static inline FRESULT validate_file_descriptor(uint64_t fd) {
    if ((fd < FAT_MAX_OPENED_FILENUM) && file_status[fd] == INUSE) {
        return FR_OK;
    }
    return FR_INVALID_PARAMETER;
}

// Checking if the descriptor is mapped to a valid object
static inline FRESULT validate_dir_descriptor(uint64_t fd) {
    if ((fd < FAT_MAX_OPENED_DIRNUM) && dir_status[fd] == INUSE) {
        return FR_OK;
    }
    return FR_INVALID_PARAMETER;
}

static FRESULT validate_and_copy_path(uint64_t path, uint64_t len, char* memory) {
    // Validate if the memory segment provided is in valid data region
    if (within_data_region(path, len) != FR_OK) {
        return FR_INVALID_PARAMETER;
    }
    // The length of the path provided most be under the upper bound
    if (len > FS_MAX_PATH_LENGTH) {
        return FR_INVALID_PARAMETER;
    }
    // Copy the string to our private memory
    memcpy(memory, fs_share + path, len);
    // Return error if the string is not NULL terminated
    memory[len] = '\0';

    return FR_OK;
}

uint32_t find_free_file_obj(void) {
    uint32_t i;
    for (i = 0; i < FAT_MAX_OPENED_FILENUM; i++) {
        if (file_status[i] == FREE) {
            return i;
        }
    }
    return i;
}

uint32_t find_free_dir_object(void) {
    uint32_t i;
    for (i = 0; i < FAT_MAX_OPENED_DIRNUM; i++) {
        if (dir_status[i] == FREE) {
            return i;
        }
    }
    return i;
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
    if (fs_status != FREE) {
        args->status = FS_STATUS_ERROR;
        return;
    }
    fs_status = INUSE;
    FRESULT RET = f_mount(&fatfs, "", 1);
    if (RET != FR_OK) {
        fs_status = FREE;
    }
    LOG_FATFS("Mounting file system result: %d\n", RET);
    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_deinitialise(void) {
    co_data_t *args = microkit_cothread_my_arg();
    if (fs_status != INUSE) {
        args->status = FS_STATUS_ERROR;
        return;
    }
    fs_status = CLEANUP;
    FRESULT RET = f_unmount("");
    if (RET == FR_OK) {
        fs_status = FREE;
    }
    else {
        fs_status = INUSE;
    }
    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_open(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.file_open.path.offset;
    uint64_t size = args->params.file_open.path.size;
    uint64_t openflag = args->params.file_open.flags;

    // Copy the name to our name buffer
    char filepath[FS_MAX_NAME_LENGTH + 1];

    // Validate string
    FRESULT RET = validate_and_copy_path(buffer, size, filepath);
    if (RET != FR_OK) {
        args->status = FS_STATUS_ERROR;
        return;
    }

    // Add open flag checking and mapping here
    LOG_FATFS("fat_open: file path: %s\n", filepath);

    uint32_t fd = find_free_file_obj();
    if (fd == FAT_MAX_OPENED_FILENUM) {
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    // Set the position to INUSE to indicate this file structure is in use
    file_status[fd] = INUSE;
    FIL* file = &(files[fd]);

    unsigned char fat_flag = map_fs_flags_to_fat_flags(openflag);

    LOG_FATFS("fat_open: fs_protocol open flag: %lu\n", openflag);
    LOG_FATFS("fat_open: fat open flag: %hhu\n", fat_flag);

    // Micropython openflag still WIP, fixes this once that is completed
    RET = f_open(file, filepath, fat_flag);

    // Error handling
    if (RET != FR_OK) {
        file_status[fd] = FREE;
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
    args->result.file_open.fd = fd;
}

void handle_file_write(void) {
    co_data_t *args = microkit_cothread_my_arg();
    uint64_t fd = args->params.file_write.fd;
    uint64_t buffer = args->params.file_write.buf.offset;
    uint64_t btw = args->params.file_write.buf.size;
    uint64_t offset = args->params.file_write.offset;

    LOG_FATFS("fat_write: bytes to be write: %lu, write offset: %lu\n", btw, offset);

    FRESULT RET = within_data_region(buffer, btw);
    if (RET != FR_OK) {
        LOG_FATFS("fat_write: invalid buffer\n");
        args->result.file_write.len_written = 0;
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }
    if ((RET = validate_file_descriptor(fd)) != FR_OK) {
        LOG_FATFS("fat_write: invalid fd provided\n");
        args->result.file_write.len_written = 0;
        args->status = FS_STATUS_INVALID_FD;
        return;
    }
    void* data = fs_share + buffer;

    FIL* file = &(files[fd]);

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        args->result.file_write.len_written = 0;
        args->status = FS_STATUS_ERROR;
        return;
    }

    uint32_t bw = 0;

    RET = f_write(file, data, btw, &bw);

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
    uint64_t fd = args->params.file_read.fd;
    uint64_t buffer = args->params.file_read.buf.offset;
    uint64_t btr = args->params.file_read.buf.size;
    uint64_t offset = args->params.file_read.offset;

    FRESULT RET;
    if ((RET = within_data_region(buffer, btr)) != FR_OK) {
        LOG_FATFS("fat_read: invalid buffer provided\n");
        args->status = FS_STATUS_INVALID_BUFFER;
        args->result.file_read.len_read = 0;
        return;
    }
    if ((RET = validate_file_descriptor(fd)) != FR_OK) {
        LOG_FATFS("fat_read: invalid fd provided\n");
        args->status = FS_STATUS_INVALID_FD;
        args->result.file_read.len_read = 0;
        return;
    }

    void* data = fs_share + buffer;

    // Maybe add validation check of file descriptor here
    FIL* file = &(files[fd]);

    LOG_FATFS("fat_read: bytes to be read: %lu, read offset: %lu\n", btr, offset);

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        args->status = FS_STATUS_ERROR;
        args->result.file_read.len_read = 0;
        return;
    }

    uint32_t br = 0;

    RET = f_read(file, data, btr, &br);

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
    uint64_t fd = args->params.file_close.fd;

    FRESULT RET = validate_file_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_close: Invalid file descriptor\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    file_status[fd] = CLEANUP;

    RET = f_close(&(files[fd]));
    if (RET == FR_OK) {
        file_status[fd] = FREE;
    }
    else {
        file_status[fd] = INUSE;
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_stat(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t path = args->params.stat.path.offset;
    uint64_t path_len = args->params.stat.path.size;
    uint64_t output_buffer = args->params.stat.buf.offset;
    uint64_t size = args->params.stat.buf.size;

    char filepath[FS_MAX_PATH_LENGTH + 1];

    FRESULT RET = within_data_region(output_buffer, sizeof(fs_stat_t));
    if (RET != FR_OK || size < sizeof(fs_stat_t)) {
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }
    if ((RET = validate_and_copy_path(path, path_len, filepath)) != FR_OK) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    fs_stat_t* file_stat = (fs_stat_t *)(fs_share + output_buffer);

    LOG_FATFS("fat_stat:asking for filename: %s\n", filepath);

    FILINFO fileinfo;
    RET = f_stat(filepath, &fileinfo);
    if (RET != FR_OK) {
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

    uint64_t fd = args->params.file_size.fd;

    uint64_t size = f_size(&(files[fd]));

    args->status = FS_STATUS_SUCCESS;
    args->result.file_size.size = size;
}

void handle_rename(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t oldpath_buffer = args->params.rename.old_path.offset;
    uint64_t oldpath_len = args->params.rename.old_path.size;

    uint64_t newpath_buffer = args->params.rename.new_path.offset;
    uint64_t newpath_len = args->params.rename.new_path.size;

    char oldpath[FS_MAX_PATH_LENGTH + 1];
    char newpath[FS_MAX_PATH_LENGTH + 1];

    // Buffer and string validation check
    FRESULT RET = validate_and_copy_path(oldpath_buffer, oldpath_len, oldpath);
    if (RET != FR_OK || (RET = validate_and_copy_path(newpath_buffer, newpath_len, newpath)) != FR_OK) {
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    RET = f_rename(oldpath, newpath);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_remove(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.file_remove.path.offset;
    uint64_t size = args->params.file_remove.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        LOG_FATFS("fat_unlink: Invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    RET = f_unlink(dirpath);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_truncate(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.file_truncate.fd;
    uint64_t len = args->params.file_truncate.length;

    FRESULT RET = validate_file_descriptor(fd);

    // FD validation check
    if (RET != FR_OK) {
        LOG_FATFS("fat_mkdir: Invalid FD\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    RET = f_lseek(&files[fd], len);

    if (RET != FR_OK) {
        LOG_FATFS("fat_truncate: Invalid file offset\n");
        args->status = FS_STATUS_ERROR;
        return;
    }

    RET = f_truncate(&files[fd]);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_dir_create(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.dir_create.path.offset;
    uint64_t size = args->params.dir_create.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        LOG_FATFS("fat_mkdir: Invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    RET = f_mkdir(dirpath);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// This seems to do the exact same thing as fat_unlink
void handle_dir_remove(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.dir_remove.path.offset;
    uint64_t size = args->params.dir_remove.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];

    // Buffer validation check
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);
    if (RET != FR_OK) {
        LOG_FATFS("fat_mkdir: Invalid path buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    RET = f_rmdir(dirpath);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_dir_open(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.dir_open.path.offset;
    uint64_t size = args->params.dir_open.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];

    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Sanity check
    if (RET != FR_OK) {
        LOG_FATFS("fat_readdir: Invalid buffer\n");
        args->status = FS_STATUS_INVALID_PATH;
        return;
    }

    uint32_t fd = find_free_dir_object();
    if (fd == FAT_MAX_OPENED_DIRNUM) {
        args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
        return;
    }

    DIR* dir = &(dirs[fd]);
    // Set the position to INUSE to indicate this file structure is in use
    dir_status[fd] = INUSE;

    LOG_FATFS("FAT opendir directory path: %s\n", dirpath);

    RET = f_opendir(dir, dirpath);

    // Error handling
    if (RET != FR_OK) {
        args->status = FS_STATUS_ERROR;
        // Free this Dir structure
        dir_status[fd] = FREE;
        return;
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
    args->result.dir_open.fd = fd;
}

void handle_dir_read(void) {
    co_data_t *args = microkit_cothread_my_arg();

    // Dir descriptor
    uint64_t fd = args->params.dir_read.fd;
    uint64_t buffer = args->params.dir_read.buf.offset;
    uint64_t size = args->params.dir_read.buf.size;

    LOG_FATFS("FAT readdir file descriptor: %lu\n", fd);

    FRESULT RET = within_data_region(buffer, size);
    // Sanity check
    if (RET != FR_OK) {
        LOG_FATFS("fat_readdir: Invalid buffer\n");
        args->status = FS_STATUS_INVALID_BUFFER;
        return;
    }
    if ((RET = validate_dir_descriptor(fd)) != FR_OK) {
        LOG_FATFS("fat_readdir: Invalid FD\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    void* name = fs_share + buffer;

    FILINFO fno;
    RET = f_readdir(&dirs[fd], &fno);


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

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// Not sure if this one is implemented correctly
void handle_dir_tell(void){
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.dir_tell.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_telldir: Invalid dir descriptor\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    DIR* dp = &(dirs[fd]);

    uint32_t offset = f_telldir(dp);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
    args->result.dir_tell.location = offset;
}

void handle_dir_rewind(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.dir_rewind.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_telldir: Invalid dir descriptor\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    RET = f_readdir(&dirs[fd], 0);

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_file_sync(void) {
    co_data_t *args = microkit_cothread_my_arg();

    // Maybe add validation check of file descriptor here
    uint64_t fd = args->params.file_sync.fd;

    FRESULT RET = validate_file_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_sync: Invalid file descriptor %lu\n", fd);
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    RET = f_sync(&(files[fd]));

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

void handle_dir_close(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.dir_close.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_closedir: Invalid dir descriptor\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    dir_status[fd] = CLEANUP;

    RET = f_closedir(&dirs[fd]);

    if (RET == FR_OK) {
        dir_status[fd] = FREE;
    }
    else {
        dir_status[fd] = INUSE;
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}

// Inefficient implementation of seekdir
// There is no function as seekdir in the current Fatfs library
// I can add one to the library but I do not want to add another layer of instability
// So just use this inefficient one for now
void handle_dir_seek(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.dir_seek.fd;
    int64_t loc = args->params.dir_seek.loc;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_seekdir: Invalid dir descriptor\n");
        args->status = FS_STATUS_INVALID_FD;
        return;
    }

    RET = f_readdir(&dirs[fd], 0);
    FILINFO fno;

    for (int64_t i = 0; i < loc; i++) {
        if (RET != FR_OK) {
            args->status = FS_STATUS_ERROR;
            return;
        }
        RET = f_readdir(&dirs[fd], &fno);
    }

    args->status = (RET == FR_OK) ? FS_STATUS_SUCCESS : FS_STATUS_ERROR;
}
