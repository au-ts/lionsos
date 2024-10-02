#include "fatfs_decl.h"
#include "ff15/source/ff.h"
#include <libmicrokitco/libmicrokitco.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <fs/protocol.h>
#include "fatfs_config.h"

/*
This file define a bunch of wrapper functions of FATFs functions so those functions can be run in the 
worker thread.
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
extern char *client_data_addr;

// Sanity check functions
// Checking if the memory region that provided by request is within valid memory region
static inline FRESULT within_data_region(uint64_t offset, uint64_t buffer_size) {
    LOG_FATFS("with_data_region check, input args: offset: %ld, buffer size: %ld\n", offset, buffer_size);
    if ((offset < DATA_REGION_SIZE) && (buffer_size <= DATA_REGION_SIZE - offset)) {
        return FR_OK;
    }
    return FR_INVALID_PARAMETER;
}

// Checking if the descriptor is mapped to a valid object
static inline FRESULT validate_file_descriptor(uint64_t fd) {
    if ((fd < MAX_OPENED_FILENUM) && file_status[fd] == INUSE) {
        return FR_OK;
    }
    return FR_INVALID_PARAMETER;
}

// Checking if the descriptor is mapped to a valid object
static inline FRESULT validate_dir_descriptor(uint64_t fd) {
    if ((fd < MAX_OPENED_DIRNUM) && dir_status[fd] == INUSE) {
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
    memcpy(memory, client_data_addr + path, len);
    // Return error if the string is not NULL terminated
    memory[len] = '\0';

    return FR_OK;
}

// Init the structure without using malloc
// Could this have potential alignment issue?
void init_metadata(uint64_t fs_metadata) {
    uint64_t base = fs_metadata;
    
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

uint32_t find_free_fs_obj(void) {
    uint32_t i;
    for (i = 0; i < MAX_FATFS; i++) {
        if (fs_status[i] == FREE) {
            return i;
        }
    }
    return i;
}

uint32_t find_free_file_obj(void) {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_FILENUM; i++) {
        if (file_status[i] == FREE) {
            return i;
        }
    }
    return i;
}

uint32_t find_free_dir_object(void) {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_DIRNUM; i++) {
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
void fat_mount(void) {
    LOG_FATFS("Mounting file system!\n");
    co_data_t *args = microkit_cothread_my_arg();
    if (fs_status[0] != FREE) {
        args->status = FR_INVALID_PARAMETER;
        return;
    }
    fs_status[0] = INUSE;
    FRESULT RET = f_mount(&(fatfs[0]), "", 1);
    if (RET != FR_OK) {
        fs_status[0] = FREE;
    }
    LOG_FATFS("Mounting file system result: %d\n", RET);
    args->status = RET;
}

void fat_unmount(void) {
    co_data_t *args = microkit_cothread_my_arg();
    if (fs_status[0] != INUSE) {
        args->status = FR_INVALID_PARAMETER;
        return;
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
}

void fat_open(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.open.path.offset;
    uint64_t size = args->params.open.path.size;
    uint64_t openflag = args->params.open.flags;
    
    // Copy the name to our name buffer
    char filepath[FS_MAX_NAME_LENGTH];

    // Validate string
    FRESULT RET = validate_and_copy_path(buffer, size, filepath);
    if (RET != FR_OK) {
        args->status = RET;
        return;
    }

    // Add open flag checking and mapping here
    LOG_FATFS("fat_open: file path: %s\n", filepath);

    uint32_t fd = find_free_file_obj();
    if (fd == MAX_OPENED_FILENUM) {
        args->status = FR_TOO_MANY_OPEN_FILES;
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
    
    args->status = RET;
    args->result.open.fd = fd;
}

void fat_pwrite(void) {
    co_data_t *args = microkit_cothread_my_arg();
    uint64_t fd = args->params.write.fd;
    uint64_t buffer = args->params.write.buf.offset;
    uint64_t btw = args->params.write.buf.size;
    uint64_t offset = args->params.write.offset;

    LOG_FATFS("fat_write: bytes to be write: %lu, write offset: %lu\n", btw, offset);

    FRESULT RET = within_data_region(buffer, btw);
    if (RET != FR_OK || (RET = validate_file_descriptor(fd)) != FR_OK) {
        LOG_FATFS("fat_write: Trying to write into invalid memory region or invalid fd provided\n");
        args->result.write.len_written = 0;
        args->status = RET;
        return;
    }

    void* data = client_data_addr + buffer;

    FIL* file = &(files[fd]);

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        args->result.write.len_written = 0;
        args->status = RET;
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

    args->status = RET;
    args->result.write.len_written = bw;
}

void fat_pread(void) {
    co_data_t *args = microkit_cothread_my_arg();
    uint64_t fd = args->params.read.fd;
    uint64_t buffer = args->params.read.buf.offset;
    uint64_t btr = args->params.read.buf.size;
    uint64_t offset = args->params.read.offset;
    
    FRESULT RET = within_data_region(buffer, btr);
    if (RET != FR_OK || (RET = validate_file_descriptor(fd)) != FR_OK) {
        LOG_FATFS("fat_read: Trying to write into invalid memory region or invalid fd provided\n");
        args->status = RET;
        args->result.read.len_read = 0;
        return;
    }

    void* data = client_data_addr + buffer;

    // Maybe add validation check of file descriptor here
    FIL* file = &(files[fd]);

    LOG_FATFS("fat_read: bytes to be read: %lu, read offset: %lu\n", btr, offset);

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        args->status = RET;
        args->result.read.len_read = 0;
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

    args->status = RET;
    args->result.read.len_read = br;
}

void fat_close(void) {
    co_data_t *args = microkit_cothread_my_arg();
    uint64_t fd = args->params.close.fd;
    
    FRESULT RET = validate_file_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_close: Invalid file descriptor\n");
        args->status = RET;
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

    args->status = RET;
}

// Mode attribute
#define Directory 0040000
#define Regularfile 0100000
#define Blockdevice 0060000
#define Socket 0140000

void fat_stat(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t path = args->params.stat.path.offset;
    uint64_t path_len = args->params.stat.path.size;
    uint64_t output_buffer = args->params.stat.buf.offset;
    uint64_t size = args->params.stat.buf.size;

    char filepath[FS_MAX_PATH_LENGTH + 1];

    FRESULT RET = within_data_region(output_buffer, sizeof(fs_stat_t));
    if (RET != FR_OK || (RET = validate_and_copy_path(path, path_len, filepath)) != FR_OK || size < sizeof(fs_stat_t)) {
        args->status = RET;
        return;
    }

    fs_stat_t* file_stat = (fs_stat_t *)(client_data_addr + output_buffer);

    LOG_FATFS("fat_stat:asking for filename: %s\n", filepath);
    
    FILINFO fileinfo;
    RET = f_stat(filepath, &fileinfo);
    if (RET != FR_OK) {
        args->status = RET;
        return;
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
}

void fat_fsize(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.fsize.fd;

    uint64_t size = f_size(&(files[fd]));

    args->status = FR_OK;
    args->result.fsize.size = size;
}

void fat_rename(void) {
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
        args->status = RET;
        return;
    }

    RET = f_rename(oldpath, newpath);
    
    args->status = RET;
}

void fat_unlink(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.unlink.path.offset;
    uint64_t size = args->params.unlink.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        LOG_FATFS("fat_unlink: Invalid memory region\n");
        args->status = RET;
        return;
    }

    RET = f_unlink(dirpath);
    

    args->status = RET;
}

void fat_truncate(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.truncate.fd;
    uint64_t len = args->params.truncate.length;
    
    FRESULT RET = validate_file_descriptor(fd);

    // FD validation check
    if (RET != FR_OK) {
        LOG_FATFS("fat_mkdir: Invalid memory region\n");
        args->status = RET;
        return;
    }

    RET = f_lseek(&files[fd], len);

    if (RET != FR_OK) {
        LOG_FATFS("fat_truncate: Invalid file offset\n");
        args->status = RET;
        return;
    }

    RET = f_truncate(&files[fd]);

    args->status = RET;
}

void fat_mkdir(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.mkdir.path.offset;
    uint64_t size = args->params.mkdir.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        LOG_FATFS("fat_mkdir: Invalid memory region\n");
        args->status = RET;
        return;
    }

    RET = f_mkdir(dirpath);

    args->status = RET;
}

// This seems to do the exact same thing as fat_unlink
void fat_rmdir(void) {
    co_data_t *args = microkit_cothread_my_arg();
    
    uint64_t buffer = args->params.rmdir.path.offset;
    uint64_t size = args->params.rmdir.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];

    // Buffer validation check
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);
    if (RET != FR_OK) {
        LOG_FATFS("fat_mkdir: Invalid memory region\n");
        args->status = RET;
        return;
    }

    RET = f_rmdir(dirpath);

    args->status = RET;
}

void fat_opendir(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t buffer = args->params.opendir.path.offset;
    uint64_t size = args->params.opendir.path.size;

    char dirpath[FS_MAX_PATH_LENGTH + 1];

    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Sanity check
    if (RET != FR_OK) {
        LOG_FATFS("fat_readdir: Invalid buffer\n");
        args->status = RET;
        return;
    }

    uint32_t fd = find_free_dir_object();
    if (fd == MAX_OPENED_DIRNUM) {
        args->status = FR_TOO_MANY_OPEN_FILES;
        return;
    }
    
    DIR* dir = &(dirs[fd]);
    // Set the position to INUSE to indicate this file structure is in use
    dir_status[fd] = INUSE;

    LOG_FATFS("FAT opendir directory path: %s\n", dirpath);

    RET = f_opendir(dir, dirpath);
    
    // Error handling
    if (RET != FR_OK) {
        args->status = RET;
        // Free this Dir structure
        dir_status[fd] = FREE;
        return;
    }

    args->status = RET;
    args->result.opendir.fd = fd;
}

void fat_readdir(void) {
    co_data_t *args = microkit_cothread_my_arg();
    
    // Dir descriptor
    uint64_t fd = args->params.readdir.fd;
    uint64_t buffer = args->params.readdir.buf.offset;
    uint64_t size = args->params.readdir.buf.size;

    LOG_FATFS("FAT readdir file descriptor: %lu\n", fd);

    FRESULT RET = within_data_region(buffer, size);
    // Sanity check
    if (RET != FR_OK || (RET = validate_dir_descriptor(fd)) != FR_OK) {
        LOG_FATFS("fat_readdir: Invalid dir descriptor or Invalid buffer\n");
        args->status = RET;
        return;
    }

    void* name = client_data_addr + buffer;

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
        LOG_FATFS("FAT readdir file name: %.*s\n", (uint32_t)len, (char*)name);
        // Hacky change the ret value to FS_STATUS_END_OF_DIRECTORY when nothing is in the directory
        if (fno.fname[0] == 0) {
            RET = FS_STATUS_END_OF_DIRECTORY;
        }
    }

    args->status = RET;
}

// Not sure if this one is implemented correctly
void fat_telldir(void){
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.telldir.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_telldir: Invalid dir descriptor\n");
        args->status = RET;
        return;
    }

    DIR* dp = &(dirs[fd]);

    uint32_t offset = f_telldir(dp);

    args->status = RET;
    args->result.telldir.location = offset;
}

void fat_rewinddir(void) {
    co_data_t *args = microkit_cothread_my_arg();
    
    uint64_t fd = args->params.rewinddir.fd;
    
    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_telldir: Invalid dir descriptor\n");
        args->status = RET;
        return;
    }

    RET = f_readdir(&dirs[fd], 0);

    args->status = RET;
}

void fat_sync(void) {
    co_data_t *args = microkit_cothread_my_arg();

    // Maybe add validation check of file descriptor here
    uint64_t fd = args->params.fsync.fd;

    FRESULT RET = validate_file_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_sync: Invalid file descriptor %lu\n", fd);
        args->status = RET;
        return;
    }

    RET = f_sync(&(files[fd]));

    args->status = RET;
}

void fat_closedir(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.closedir.fd;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_closedir: Invalid dir descriptor\n");
        args->status = RET;
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

    args->status = RET;
}

// Inefficient implementation of seekdir
// There is no function as seekdir in the current Fatfs library
// I can add one to the library but I do not want to add another layer of instability
// So just use this inefficient one for now
void fat_seekdir(void) {
    co_data_t *args = microkit_cothread_my_arg();

    uint64_t fd = args->params.seekdir.fd;
    int64_t loc = args->params.seekdir.loc;

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        LOG_FATFS("fat_seekdir: Invalid dir descriptor\n");
        args->status = RET;
        return;
    }
    
    RET = f_readdir(&dirs[fd], 0);
    FILINFO fno;

    for (int64_t i = 0; i < loc; i++) {
        if (RET != FR_OK) {
            args->status = RET;
            return;
        }
        RET = f_readdir(&dirs[fd], &fno);
    }

    args->status = RET;
}
