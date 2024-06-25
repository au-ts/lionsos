#include "AsyncFATFs.h"
#include "ff15/source/ff.h"
#include "FiberPool/FiberPool.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../../include/lions/fs/protocol.h"
#include "fatfs_config.h"

#ifdef FS_DEBUG_PRINT
#include "../../../dep/sddf/include/sddf/util/printf.h"
#endif
/*
This file define a bunch of wrapper functions of FATFs functions so thos functions can be run in the 
coroutine.
*/

typedef enum : uint8_t {
    FREE = 0,
    INUSE = 1,
    CLEANUP = 2,
} Descriptor_Status;

void Function_Fill_Response(void* data, FRESULT result, uint64_t RETDATA, uint64_t RETDATA2) {
    uint64_t *args = (uint64_t *) data;
    args[Status_bit] = result;
    args[First_data_bit] = RETDATA;
    args[Second_data_bit] = RETDATA2;
    return;
}

Descriptor_Status* Fs_Status;
FATFS* Fatfs;
Descriptor_Status* File_Status;
FIL* Files;
Descriptor_Status* Dir_Status;
DIR* Dirs;

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
    if ((fd < MAX_OPENED_FILENUM) && File_Status[fd] == INUSE) {
        return FR_OK;
    }
    else return FR_INVALID_PARAMETER;
}

// Checking if the descriptor is mapped to a valid object
static inline FRESULT validate_dir_descriptor(uint64_t fd) {
    if ((fd < MAX_OPENED_DIRNUM) && Dir_Status[fd] == INUSE) {
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
    if (len > MAX_PATH_LEN) {
        return FR_INVALID_PARAMETER;
    }
    // Copy the string to our private memory
    memcpy(memory, (void*)(path + client_data_offset), len);
    // Return error if the string is not NULL terminated
    if (memory[len - 1] != '\n') {
        return FR_INVALID_PARAMETER;
    }
    return FR_OK;
}

// Init the structure without using malloc
// Could this have potential alignment issue?
void init_metadata(void* fs_metadata) {
    uint64_t base = (uint64_t)fs_metadata;
    
    // Allocate memory for Fs_Status
    Fs_Status = (Descriptor_Status*)base;
    base += sizeof(Descriptor_Status) * MAX_FATFS;
    
    // Allocate memory for FATFS
    Fatfs = (FATFS*)base;
    base += sizeof(FATFS) * MAX_FATFS;
    
    // Allocate memory for File_Status
    File_Status = (Descriptor_Status*)base;
    base += sizeof(Descriptor_Status) * MAX_OPENED_FILENUM;
    
    // Allocate memory for Files
    Files = (FIL*)base;
    base += sizeof(FIL) * MAX_OPENED_FILENUM;
    
    // Allocate memory for Dir_Status
    Dir_Status = (Descriptor_Status*)base;
    base += sizeof(Descriptor_Status) * MAX_OPENED_DIRNUM;
    
    // Allocate memory for Dirs
    Dirs = (DIR*)base;
}

uint32_t Find_FreeFs() {
    uint32_t i;
    for (i = 0; i < MAX_FATFS; i++) {
        if (Fs_Status[i] == FREE) {
            return i;
        }
    }
    return i;
}

uint32_t Find_FreeFile() {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_FILENUM; i++) {
        if (File_Status[i] == FREE) {
            return i;
        }
    }
    return i;
}

uint32_t Find_FreeDir() {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_DIRNUM; i++) {
        if (Dir_Status[i] == FREE) {
            return i;
        }
    }
    return i;
}

// Change here later to support more than one FAT volumes
void fat_mount() {
    uint64_t *args = Fiber_GetArgs();
    if (Fs_Status[0] != FREE) {
        Function_Fill_Response(args, FR_INVALID_PARAMETER, 0, 0);
        Fiber_kill();
    }
    Fs_Status[0] = INUSE;
    FRESULT RET = f_mount(&(Fatfs[0]), "", 1);
    if (RET != FR_OK) {
        Fs_Status[0] = FREE;
    }
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_unmount() {
    uint64_t *args = Fiber_GetArgs();
    if (Fs_Status[0] != INUSE) {
        Function_Fill_Response(args, FR_INVALID_PARAMETER, 0, 0);
        Fiber_kill();
    }
    Fs_Status[0] = CLEANUP;
    FRESULT RET = f_unmount("");
    if (RET == FR_OK) {
        Fs_Status[0] = FREE;
    }
    else {
        Fs_Status[0] = INUSE;
    }
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_open() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t buffer = args[0];
    uint64_t size = args[1];
    uint64_t openflag = args[2];
    
    // Copy the name to our name buffer
    char filepath[MAX_PATH_LEN];

    // Validate string
    FRESULT RET = validate_and_copy_path(buffer, size, filepath);
    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    // Add open flag checking and mapping here
    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_open: file path: %s\n", filepath);
    sddf_printf("fat_open: open flag: %lu\n", openflag);
    #endif

    uint32_t fd = Find_FreeFile();
    if (fd == MAX_OPENED_FILENUM) {
        Function_Fill_Response(args, FR_TOO_MANY_OPEN_FILES, 0, 0);
        Fiber_kill();
    }

    // Set the position to INUSE to indicate this file structure is in use
    File_Status[fd] = INUSE;
    FIL* file = &(Files[fd]);

    // Micropython seems to always use open flag FA_CREATE_ALWAYS, so a hack here is to always use FA_OPEN_ALWAYS
    // It should be changed back after Micropython fix this
    // FRESULT RET = f_open(file, filepath, args[2]);
    RET = f_open(file, filepath, (FA_OPEN_ALWAYS|FA_READ|FA_WRITE));
    
    // Error handling
    if (RET != FR_OK) {
        File_Status[fd] = FREE;
    }

    Function_Fill_Response(args, RET, fd, 0);
    Fiber_kill();
}

void fat_pwrite() {
    uint64_t *args = Fiber_GetArgs();
    uint64_t fd = args[0];
    uint64_t buffer = args[1];
    uint64_t btw = args[2];
    uint64_t offset = args[3];

    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_write: bytes to be write: %lu, read offset: %lu\n", btw, offset);
    #endif

    FRESULT RET = within_data_region(buffer, btw);
    if (RET != FR_OK || (RET = validate_file_descriptor(fd)) != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_write: Trying to write into invalid memory region or invalid fd provided\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    void* data = (void *) (buffer + client_data_offset);

    // Maybe add validation check of file descriptor here
    FIL* file = &(Files[fd]);

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
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

    Function_Fill_Response(args, RET, bw, 0);
    Fiber_kill();
}

void fat_pread() {
    uint64_t *args = Fiber_GetArgs();
    uint64_t fd = args[0];
    uint64_t buffer = args[1];
    uint64_t btr = args[2];
    uint64_t offset = args[3];
    
    FRESULT RET = within_data_region(buffer, btr);
    if (RET != FR_OK || (RET = validate_file_descriptor(fd)) != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_read: Trying to write into invalid memory region or invalid fd provided\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    void* data = (void *) (buffer + client_data_offset);

    // Maybe add validation check of file descriptor here
    FIL* file = &(Files[fd]);

    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_read: bytes to be read: %lu, read offset: %lu\n", btr, offset);
    #endif

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
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

    Function_Fill_Response(args, RET, br, 0);
    Fiber_kill();
}

void fat_close() {
    uint64_t *args = Fiber_GetArgs();
    uint64_t fd = args[0];
    
    FRESULT RET = validate_file_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
            sddf_printf("fat_close: Invalid file descriptor\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    File_Status[fd] = CLEANUP;

    RET = f_close(&(Files[fd]));
    if (RET == FR_OK) {
        File_Status[fd] = FREE;
    }
    else {
        File_Status[fd] = INUSE;
    }
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

// Mode attribute
#define Directory 0040000
#define Regularfile 0100000
#define Blockdevice 0060000
#define Socket 0140000

void fat_stat() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t buffer = args[0];
    uint64_t size = args[1];
    uint64_t output_buffer = args[2];

    char filepath[MAX_PATH_LEN];

    FRESULT RET = within_data_region(output_buffer, sizeof(struct sddf_fs_stat_64));
    if (RET != FR_OK || (RET = validate_and_copy_path(buffer, size, filepath)) != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }
    
    struct sddf_fs_stat_64* file_stat = (void*)(output_buffer + client_data_offset);

    // Should I add valid String check here?
    #ifdef FS_DEBUG_PRINT
    sddf_printf("fat_stat:asking for filename: %s\n", filepath);
    #endif
    
    FILINFO fileinfo;
    RET = f_stat(filepath, &fileinfo);
    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }
    
    memset(file_stat, 0, sizeof(struct sddf_fs_stat_64));
    file_stat->atime = fileinfo.ftime;
    file_stat->ctime = fileinfo.ftime;
    file_stat->mtime = fileinfo.ftime;
    
    file_stat->size = fileinfo.fsize;

// Now we have only one fat volume, so we can hard code it here
    file_stat->blksize = Fatfs[0].ssize;

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

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_rename() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t oldpath_buffer = args[0];
    uint64_t oldpath_len = args[1];

    uint64_t newpath_buffer = args[2];
    uint64_t newpath_len = args[3];

    char oldpath[MAX_PATH_LEN];
    char newpath[MAX_PATH_LEN];

    // Buffer and string validation check
    FRESULT RET = validate_and_copy_path(oldpath_buffer, oldpath_len, oldpath);
    if (RET != FR_OK || (RET = validate_and_copy_path(newpath_buffer, newpath_len, newpath)) != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    RET = f_rename(oldpath, newpath);
   
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_unlink() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t buffer = args[0];
    uint64_t size = args[1];

    char dirpath[MAX_PATH_LEN];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_unlink: Invalid memory region\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    RET = f_unlink(dirpath);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_mkdir() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t buffer = args[0];
    uint64_t size = args[1];

    char dirpath[MAX_PATH_LEN];
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Buffer validation check
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_mkdir: Invalid memory region\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    RET = f_mkdir(dirpath);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

// This seems to do the exact same thing as fat_unlink
void fat_rmdir() {
    uint64_t *args = Fiber_GetArgs();
    
    uint64_t buffer = args[0];
    uint64_t size = args[1];

    char dirpath[MAX_PATH_LEN];

    // Buffer validation check
    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_mkdir: Invalid memory region\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    RET = f_rmdir(dirpath);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_opendir() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t buffer = args[0];
    uint64_t size = args[1];

    char dirpath[MAX_PATH_LEN];

    FRESULT RET = validate_and_copy_path(buffer, size, dirpath);

    // Sanity check
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_readdir: Invalid dir descriptor or Invalid buffer\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    uint32_t fd = Find_FreeDir();
    if (fd == MAX_OPENED_DIRNUM) {
        Function_Fill_Response(args, FR_TOO_MANY_OPEN_FILES, 0, 0);
        Fiber_kill();
    }
    
    DIR* dir = &(Dirs[fd]);
    // Set the position to INUSE to indicate this file structure is in use
    Dir_Status[fd] = INUSE;

    #ifdef FS_DEBUG_PRINT
    sddf_printf("FAT opendir directory path: %s\n", dirpath);
    #endif

    RET = f_opendir(dir, dirpath);
    
    // Error handling
    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        // Free this Dir structure
        Dir_Status[fd] = FREE;
        Fiber_kill();
    }

    Function_Fill_Response(args, RET, fd, 0);
    Fiber_kill();
}

void fat_readdir() {
    uint64_t *args = Fiber_GetArgs();
    
    // Dir descriptor
    uint64_t fd = args[0];
    uint64_t buffer = args[1];
    uint64_t size = args[2];

    char path[MAX_PATH_LEN];

    #ifdef FS_DEBUG_PRINT
    sddf_printf("FAT readdir file descriptor: %lu\n", fd);
    #endif

    FRESULT RET = within_data_region(buffer, size);
    // Sanity check
    if (RET != FR_OK || (RET = validate_dir_descriptor(fd)) != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_readdir: Invalid dir descriptor or Invalid buffer\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    void* name = (void*)(buffer + client_data_offset);
    
    FILINFO fno;
    RET = f_readdir(&Dirs[fd], &fno);

    // The buffer most have a size that is minimum length of the name plus one
    if (RET == FR_OK && size <= strlen(fno.fname)) {
        RET = FR_INVALID_PARAMETER;
    }
    
    if (RET == FR_OK) {
        strcpy(name, fno.fname);
        #ifdef FS_DEBUG_PRINT
        sddf_printf("FAT readdir file name: %s\n", (char*)name);
        #endif
    }

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

// Not sure if this one is implemented correctly
void fat_telldir(){
    uint64_t *args = Fiber_GetArgs();

    uint64_t fd = args[0];

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_telldir: Invalid dir descriptor\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    DIR* dp = &(Dirs[fd]);

    uint32_t offset = f_telldir(dp);

    Function_Fill_Response(args, FR_OK, offset, 0);
    Fiber_kill();
}

void fat_rewinddir() {
    uint64_t *args = Fiber_GetArgs();
    
    uint64_t fd = args[0];
    
    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_telldir: Invalid dir descriptor\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    RET = f_readdir(&Dirs[fd], 0);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_sync() {
    uint64_t *args = Fiber_GetArgs();

    // Maybe add validation check of file descriptor here
    uint64_t fd = args[0];

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_sync: Invalid file descriptor\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    RET = f_sync(&(Files[fd]));

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_closedir() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t fd = args[0];

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_closedir: Invalid dir descriptor\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }

    Dir_Status[fd] = CLEANUP;

    RET = f_closedir(&Dirs[fd]);

    if (RET == FR_OK) {
        Dir_Status[fd] = FREE;
    }
    else {
        Dir_Status[fd] = INUSE;
    }

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

// Inefficient implementation of seekdir
// There is no function as seekdir in the current Fatfs library
// I can add one to the library but I do not want to add another layer of instability
// So just use this inefficient one for now
void fat_seekdir() {
    uint64_t *args = Fiber_GetArgs();

    uint64_t fd = args[0];
    int64_t loc = args[1];

    FRESULT RET = validate_dir_descriptor(fd);
    if (RET != FR_OK) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("fat_seekdir: Invalid dir descriptor\n");
        #endif
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }
    
    RET = f_readdir(&Dirs[fd], 0);
    FILINFO fno;

    for (int64_t i = 0; i < loc; i++) {
        if (RET != FR_OK) {
            Function_Fill_Response(args, RET, 0, 0);
            Fiber_kill();
        }
        RET = f_readdir(&Dirs[fd], &fno);
    }
    
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}