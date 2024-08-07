#include "fatfs_decl.h"
#include "ff15/source/ff.h"
#include "co_helper.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../../include/lions/fs/protocol.h"
#include "fatfs_config.h"

#ifdef FS_DEBUG_PRINT
#include "../../../dep/sddf/include/sddf/util/printf.h"
#endif
/*
This file define a bunch of wrapper functions of FATFs functions so those functions can be run in the 
coroutine.
*/

typedef enum : uint8_t {
    FREE = 0,
    INUSE = 1,
    CLEANUP = 2,
} descriptor_status;

descriptor_status* fs_status;
FATFS* fatfs;
descriptor_status* file_status;
FIL* files;
descriptor_status* dir_status;
DIR* dirs;

// Data buffer offset
extern uintptr_t client_data_offset;

// Sanity check functions
// Checking if the memory region that provided by request is within valid memory region
static inline FRESULT within_data_region(uint64_t offset, uint64_t buffer_size) {
    if ((offset < DATA_REGION_SIZE) && (buffer_size <= DATA_REGION_SIZE - offset)) {
        return FR_OK;
    }
    else return FR_INVALID_PARAMETER;
}

// Checking if the descriptor is mapped to a valid object
static inline FRESULT validate_file_descriptor(uint64_t fd) {
    if ((fd < MAX_OPENED_FILENUM) && file_status[fd] == INUSE) {
        return FR_OK;
    }
    else return FR_INVALID_PARAMETER;
}

// Checking if the descriptor is mapped to a valid object
static inline FRESULT validate_dir_descriptor(uint64_t fd) {
    if ((fd < MAX_OPENED_DIRNUM) && dir_status[fd] == INUSE) {
        return FR_OK;
    }
    else return FR_INVALID_PARAMETER;
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
    memcpy(memory, (void*)(path + client_data_offset), len);
    // Return error if the string is not NULL terminated
    memory[len] = '\0';

    return FR_OK;
}

// Init the structure without using malloc
// Could this have potential alignment issue?
void init_metadata(void* fs_metadata) {
    uint64_t base = (uint64_t)fs_metadata;
    
    // Allocate memory for fs_status
    fs_status = (descriptor_status*)base;
    base += sizeof(descriptor_status) * MAX_FATFS;
    
    // Allocate memory for FATFS
    fatfs = (FATFS*)base;
    base += sizeof(FATFS) * MAX_FATFS;
    
    // Allocate memory for file_status
    file_status = (descriptor_status*)base;
    base += sizeof(descriptor_status) * MAX_OPENED_FILENUM;
    
    // Allocate memory for files
    files = (FIL*)base;
    base += sizeof(FIL) * MAX_OPENED_FILENUM;
    
    // Allocate memory for Dir_Status
    dir_status = (descriptor_status*)base;
    base += sizeof(descriptor_status) * MAX_OPENED_DIRNUM;
    
    // Allocate memory for dirs
    dirs = (DIR*)base;
}

uint32_t Find_FreeFs(void) {
    uint32_t i;
    for (i = 0; i < MAX_FATFS; i++) {
        if (fs_status[i] == FREE) {
            return i;
        }
    }
    return i;
}

uint32_t Find_FreeFile(void) {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_FILENUM; i++) {
        if (file_status[i] == FREE) {
            return i;
        }
    }
    return i;
}

uint32_t Find_FreeDir(void) {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_DIRNUM; i++) {
        if (dir_status[i] == FREE) {
            return i;
        }
    }
    return i;
}

// Change here later to support more than one FAT volumes
void fat_mount(void) {
    co_data_t *args = co_get_args();
    if (fs_status[0] != FREE) {
        args->status = FR_INVALID_PARAMETER;
        co_kill();
    }
    fs_status[0] = INUSE;
    FRESULT RET = f_mount(&(fatfs[0]), "", 1);
    if (RET != FR_OK) {
        fs_status[0] = FREE;
    }
    args->status = RET;
    co_kill();
}

void fat_unmount(void) {
    co_data_t *args = co_get_args();
    if (fs_status[0] != INUSE) {
        args->status = FR_INVALID_PARAMETER;
        co_kill();
    }
    fs_status[0] = CLEANUP;
    FRESULT RET = f_unmount("");
    if (RET == FR_OK) {
        fs_status[0] = FREE;
    }
    else {
        fs_status[0] = INUSE;
    }
    args->status = RET;
    co_kill();
}

void fat_open(void) {
    co_data_t *args = co_get_args();

    uint64_t buffer = args->params.open.path.offset;
    uint64_t size = args->params.open.path.size;
    uint64_t openflag = args->params.open.flags;
    
    // Copy the name to our name buffer
    char filepath[FS_MAX_NAME_LENGTH];

    // Validate string
    FRESULT RET = validate_and_copy_path(buffer, size, filepath);
    if (RET != FR_OK) {
        args->status = RET;
        co_kill();
    }

    // Add open flag checking and mapping here
    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_open: file path: %s\n", filepath);
    sddf_printf("fat_open: open flag: %lu\n", openflag);
    #endif

    uint32_t fd = Find_FreeFile();
    if (fd == MAX_OPENED_FILENUM) {
        args->status = FR_TOO_MANY_OPEN_FILES;
        co_kill();
    }

    // Set the position to INUSE to indicate this file structure is in use
    file_status[fd] = INUSE;
    FIL* file = &(files[fd]);

    // Micropython openflag still WIP, fixes this once that is completed
    RET = f_open(file, filepath, (FA_OPEN_ALWAYS|FA_READ|FA_WRITE));
    
    // Error handling
    if (RET != FR_OK) {
        file_status[fd] = FREE;
    }
    
    args->status = RET;
    args->result.open.fd = fd;

    co_kill();
}

void fat_pwrite(void) {
    co_data_t *args = co_get_args();
    uint64_t fd = args->params.write.fd;
    uint64_t buffer = args->params.write.buf.offset;
    uint64_t btw = args->params.write.buf.size;
    uint64_t offset = args->params.write.offset;

    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_write: bytes to be write: %lu, read offset: %lu\n", btw, offset);
    #endif

    FRESULT RET = within_data_region(buffer, btw);
    if (RET != FR_OK || (RET = validate_file_descriptor(fd)) != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_write: Trying to write into invalid memory region or invalid fd provided\n");
        #endif
        args->result.write.len_written = 0;
        args->status = RET;
        co_kill();
    }

    void* data = (void *) (buffer + client_data_offset);

    // Maybe add validation check of file descriptor here
    FIL* file = &(files[fd]);

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        args->result.write.len_written = 0;
        args->status = RET;
        co_kill();
    }
    
    uint32_t bw = 0;

    RET = f_write(file, data, btw, &bw);

    #ifdef FS_DEBUG_PRINT
    if (RET == FR_OK) {
        sddf_printf("fat_write: byte written: %u, content written: \n%.*s\n", bw, bw, (char *)data);
    }
    else {
        sddf_printf("fat_write: error");
    }
    #endif
    
    args->status = RET;
    args->result.write.len_written = bw;

    co_kill();
}

void fat_pread(void) {
    co_data_t *args = co_get_args();
    uint64_t fd = args->params.read.fd;
    uint64_t buffer = args->params.read.buf.offset;
    uint64_t btr = args->params.read.buf.size;
    uint64_t offset = args->params.read.offset;
    
    FRESULT RET = within_data_region(buffer, btr);
    if (RET != FR_OK || (RET = validate_file_descriptor(fd)) != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_read: Trying to write into invalid memory region or invalid fd provided\n");
        #endif
        args->status = RET;
        args->result.read.len_read = 0;
        co_kill();
    }

    void* data = (void *) (buffer + client_data_offset);

    // Maybe add validation check of file descriptor here
    FIL* file = &(files[fd]);

    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_read: bytes to be read: %lu, read offset: %lu\n", btr, offset);
    #endif

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        args->status = RET;
        args->result.read.len_read = 0;
        co_kill();
    }
    
    uint32_t br = 0;

    RET = f_read(file, data, btr, &br);

    #ifdef FS_DEBUG_PRINT
    if (RET == FR_OK) {
        sddf_printf("fat_read: byte read: %u, content read: \n%.*s\n", br, br, (char *)data);
    }
    else {
        sddf_printf("fat_read: error");
    }
    #endif

    args->status = RET;
    args->result.read.len_read = br;

    co_kill();
}

void fat_close(void) {
    co_data_t *args = co_get_args();
    uint64_t fd = args->params.close.fd;
    
    FRESULT RET = validate_file_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
            sddf_printf("fat_close: Invalid file descriptor\n");
        #endif
        args->status = RET;
        co_kill();
    }

    file_status[fd] = CLEANUP;

    RET = f_close(&(files[fd]));
    if (RET == FR_OK) {
        file_status[fd] = FREE;
    }
    else {
        file_status[fd] = INUSE;
    }

    args->status = RET;
    co_kill();
}

// Mode attribute
#define Directory 0040000
#define Regularfile 0100000
#define Blockdevice 0060000
#define Socket 0140000

void fat_stat(void) {
    co_data_t *args = co_get_args();

    uint64_t buffer = args->params.stat.path.offset;
    uint64_t size = args->params.stat.buf.size;
    uint64_t output_buffer = args->params.stat.buf.offset;

    char filepath[FS_MAX_PATH_LENGTH + 1];

    FRESULT RET = within_data_region(output_buffer, sizeof(fs_stat_t));
    if (RET != FR_OK || (RET = validate_and_copy_path(buffer, size, filepath)) != FR_OK) {
        args->status = RET;
        co_kill();
    }
    
    fs_stat_t* file_stat = (void*)(output_buffer + client_data_offset);

    // Should I add valid String check here?
    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_stat:asking for filename: %s\n", filepath);
    #endif
    
    FILINFO fileinfo;
    RET = f_stat(filepath, &fileinfo);
    if (RET != FR_OK) {
        args->status = RET;
        co_kill();
    }
    
    memset(file_stat, 0, sizeof(fs_stat_t));
    file_stat->atime = fileinfo.ftime;
    file_stat->ctime = fileinfo.ftime;
    file_stat->mtime = fileinfo.ftime;
    
    file_stat->size = fileinfo.fsize;

// Now we have only one fat volume, so we can hard code it here
    file_stat->blksize = fatfs[0].ssize;

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
    
    args->status = RET;
    co_kill();
}

void fat_fsize(void) {
    co_data_t *args = co_get_args();

    uint64_t fd = args->params.fsize.fd;

    uint64_t size = f_size(&(files[fd]));

    args->status = FR_OK;
    args->result.fsize.size = size;

    co_kill();
}

void fat_rename(void) {
    co_data_t *args = co_get_args();

    uint64_t oldpath_buffer = args->params.rename.old_path.offset;
    uint64_t oldpath_len = args->params.rename.old_path.size;

    uint64_t newpath_buffer = args->params.rename.new_path.offset;
    uint64_t newpath_len = args->params.rename.new_path.size;

    char oldpath[FS_MAX_PATH_LENGTH + 1];
    char newpath[FS_MAX_PATH_LENGTH + 1];

    // Buffer and string validation check
    FRESULT RET = validate_and_copy_path(oldpath_buffer, oldpath_len, oldpath);
    if (RET != FR_OK || (RET = validate_and_copy_path(newpath_buffer, newpath_len, newpath)) != FR_OK) {
        args->status = RET;
        co_kill();
    }

    RET = f_rename(oldpath, newpath);
    
    args->status = RET;
    co_kill();
}

void fat_unlink(void) {
    co_data_t *args = co_get_args();

    uint64_t buffer = args->params.unlink.path.offset;
    uint64_t size = args->params.unlink.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_unlink: Invalid memory region\n");
        #endif
        args->status = RET;
        co_kill();
    }

    RET = f_unlink(dirpath);
    

    args->status = RET;
    co_kill();
}

void fat_mkdir(void) {
    co_data_t *args = co_get_args();

    uint64_t buffer = args->params.mkdir.path.offset;
    uint64_t size = args->params.mkdir.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_mkdir: Invalid memory region\n");
        #endif
        args->status = RET;
        co_kill();
    }

    RET = f_mkdir(dirpath);

    args->status = RET;
    co_kill();
}

// This seems to do the exact same thing as fat_unlink
void fat_rmdir(void) {
    co_data_t *args = co_get_args();
    
    uint64_t buffer = args->params.mkdir.path.offset;
    uint64_t size = args->params.mkdir.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];

    // Buffer validation check
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_mkdir: Invalid memory region\n");
        #endif
        args->status = RET;
        co_kill();
    }

    RET = f_rmdir(dirpath);

    args->status = RET;
    co_kill();
}

void fat_opendir(void) {
    co_data_t *args = co_get_args();

    uint64_t buffer = args->params.mkdir.path.offset;
    uint64_t size = args->params.mkdir.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];

    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Sanity check
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_readdir: Invalid buffer\n");
        #endif
        args->status = RET;
        co_kill();
    }

    uint32_t fd = Find_FreeDir();
    if (fd == MAX_OPENED_DIRNUM) {
        args->status = FR_TOO_MANY_OPEN_FILES;
        co_kill();
    }
    
    DIR* dir = &(dirs[fd]);
    // Set the position to INUSE to indicate this file structure is in use
    dir_status[fd] = INUSE;

    #ifdef FS_DEBUG_PRINT
    sddf_printf("FAT opendir directory path: %s\n", dirpath);
    #endif

    RET = f_opendir(dir, dirpath);
    
    // Error handling
    if (RET != FR_OK) {
        args->status = RET;
        // Free this Dir structure
        dir_status[fd] = FREE;
        co_kill();
    }

    args->status = RET;
    args->result.opendir.fd = fd;
    co_kill();
}

void fat_readdir(void) {
    co_data_t *args = co_get_args();
    
    // Dir descriptor
    uint64_t fd = args->params.readdir.fd;
    uint64_t buffer = args->params.readdir.buf.offset;
    uint64_t size = args->params.readdir.buf.size;

    char path[FS_MAX_PATH_LENGTH + 1];

    #ifdef FS_DEBUG_PRINT
    sddf_printf("FAT readdir file descriptor: %lu\n", fd);
    #endif

    FRESULT RET = within_data_region(buffer, size);
    // Sanity check
    if (RET != FR_OK || (RET = validate_dir_descriptor(fd)) != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_readdir: Invalid dir descriptor or Invalid buffer\n");
        #endif
        args->status = RET;
        co_kill();
    }

    void* name = (void*)(buffer + client_data_offset);
    
    FILINFO fno;
    RET = f_readdir(&dirs[fd], &fno);
    

    uint64_t len = strlen(fno.fname);
    // The buffer most have a size that is minimum length of the name plus one
    if (RET == FR_OK && size < len) {
        RET = FR_INVALID_PARAMETER;
    }
    
    if (RET == FR_OK) {
        args->result.readdir.path_len = len;
        memcpy(name, fno.fname, len);
        #ifdef FS_DEBUG_PRINT
        sddf_printf("FAT readdir file name: %s\n", (char*)name);
        #endif
    }

    args->status = RET;

    co_kill();
}

// Not sure if this one is implemented correctly
void fat_telldir(void){
    co_data_t *args = co_get_args();

    uint64_t fd = args->params.telldir.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_telldir: Invalid dir descriptor\n");
        #endif
        args->status = RET;
        co_kill();
    }

    DIR* dp = &(dirs[fd]);

    uint32_t offset = f_telldir(dp);

    args->status = RET;
    args->result.telldir.location = offset;

    co_kill();
}

void fat_rewinddir(void) {
    co_data_t *args = co_get_args();
    
    uint64_t fd = args->params.rewinddir.fd;
    
    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_telldir: Invalid dir descriptor\n");
        #endif
        args->status = RET;
        co_kill();
    }

    RET = f_readdir(&dirs[fd], 0);

    args->status = RET;
    co_kill();
}

void fat_sync(void) {
    co_data_t *args = co_get_args();

    // Maybe add validation check of file descriptor here
    uint64_t fd = args->params.fsync.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_sync: Invalid file descriptor\n");
        #endif
        args->status = RET;
        co_kill();
    }

    RET = f_sync(&(files[fd]));

    args->status = RET;
    co_kill();
}

void fat_closedir(void) {
    co_data_t *args = co_get_args();

    uint64_t fd = args->params.closedir.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_closedir: Invalid dir descriptor\n");
        #endif
        args->status = RET;
        co_kill();
    }

    dir_status[fd] = CLEANUP;

    RET = f_closedir(&dirs[fd]);

    if (RET == FR_OK) {
        dir_status[fd] = FREE;
    }
    else {
        dir_status[fd] = INUSE;
    }

    args->status = RET;
    co_kill();
}

// Inefficient implementation of seekdir
// There is no function as seekdir in the current Fatfs library
// I can add one to the library but I do not want to add another layer of instability
// So just use this inefficient one for now
void fat_seekdir(void) {
    co_data_t *args = co_get_args();

    uint64_t fd = args->params.seekdir.fd;
    int64_t loc = args->params.seekdir.loc;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_seekdir: Invalid dir descriptor\n");
        #endif
        args->status = RET;
        co_kill();
    }
    
    RET = f_readdir(&dirs[fd], 0);
    FILINFO fno;

    for (int64_t i = 0; i < loc; i++) {
        if (RET != FR_OK) {
            args->status = RET;
            co_kill();
        }
        RET = f_readdir(&dirs[fd], &fno);
    }

    args->status = RET;
    co_kill();
}