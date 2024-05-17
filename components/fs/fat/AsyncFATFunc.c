#include "AsyncFATFs.h"
#include "ff15/source/ff.h"
#include "FiberPool/FiberPool.h"
#include <stdbool.h>
#include <string.h>
#include "../../../include/lions/fs/protocol.h"

/*
This file define a bunch of wrapper functions of FATFs functions so thos functions can be run in the 
coroutine.
*/

void Function_Fill_Response(void* data, FRESULT result, uint64_t RETDATA, uint64_t RETDATA2) {
    uint64_t *args = (uint64_t *) data;
    args[Status_bit] = result;
    args[First_data_bit] = RETDATA;
    args[Second_data_bit] = RETDATA2;
    return;
}

FATFS* Fatfs;
bool* File_BitList;
FIL* Files;
bool* Dirs_BitList;
DIR* Dirs;

// Init the structure without using malloc
void init_metadata(void* fs_metadata) {
    char* base = (char*)fs_metadata;

    Fatfs = (FATFS*)base;
    File_BitList = (bool*)(base + sizeof(FATFS));
    Files = (FIL*)(base + sizeof(FATFS) + sizeof(bool) * MAX_OPENED_FILENUM);
    Dirs_BitList = (bool*)(base + sizeof(FATFS) + sizeof(bool) * MAX_OPENED_FILENUM + sizeof(FIL) * MAX_OPENED_FILENUM);
    Dirs = (DIR*)(base + sizeof(FATFS) + sizeof(bool) * MAX_OPENED_FILENUM + sizeof(FIL) * MAX_OPENED_FILENUM + sizeof(bool) * MAX_OPENED_DIRNUM);
}

uint32_t Find_FreeFile() {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_FILENUM; i++) {
        if (File_BitList[i] == 0) {
            return i;
        }
    }
    return i;
}

uint32_t Find_FreeDir() {
    uint32_t i;
    for (i = 0; i < MAX_OPENED_DIRNUM; i++) {
        if (Dirs_BitList[i] == 0) {
            return i;
        }
    }
    return i;
}

// Change here later to support more than one FAT volumes
void fat_mount() {
    uint64_t *args = Fiber_GetArgs();
    FRESULT RET = f_mount(&(Fatfs[0]), "", 1);
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_unmount() {
    uint64_t *args = Fiber_GetArgs();
    FRESULT RET = f_unmount("");
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_open() {
    uint64_t *args = Fiber_GetArgs();
    uint32_t fd = Find_FreeFile();
    if (fd == MAX_OPENED_FILENUM) {
        Function_Fill_Response(args, FR_TOO_MANY_OPEN_FILES, 0, 0);
        Fiber_kill();
    }
    FIL* file = &(Files[fd]);

    FRESULT RET = f_open(file, (void *)args[0], args[1]);
    
    // Error handling
    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }
    
    // Set the position to 1 to indicate this file structure is in use
    File_BitList[fd] = 1;

    Function_Fill_Response(args, RET, fd, 0);
    Fiber_kill();
}

void fat_pwrite() {
    uint64_t *args = Fiber_GetArgs();
    uint64_t fd = args[0];
    void* data = (void *) args[1];
    uint64_t btw = args[2];
    uint64_t offset = args[3];
    
    // Maybe add validation check of file descriptor here
    FIL* file = &(Files[fd]);
    FRESULT RET = FR_TIMEOUT;

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }
    
    uint32_t bw = 0;

    RET = f_write(file, data, btw, &bw);

    Function_Fill_Response(args, RET, bw, 0);
    Fiber_kill();
}

void fat_pread() {
    uint64_t *args = Fiber_GetArgs();
    uint64_t fd = args[0];
    void* data = (void *) args[1];
    uint64_t btr = args[2];
    uint64_t offset = args[3];
    
    // Maybe add validation check of file descriptor here
    FIL* file = &(Files[fd]);
    FRESULT RET = FR_TIMEOUT;

    RET = f_lseek(file, offset);

    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }
    
    uint32_t br = 0;

    RET = f_read(file, data, btr, &br);

    Function_Fill_Response(args, RET, br, 0);
    Fiber_kill();
}

void fat_close() {
    uint64_t *args = Fiber_GetArgs();
    uint64_t fd = args[0];

    if (fd < 0 || fd >= MAX_OPENED_FILENUM) {
        Function_Fill_Response(args, FR_INVALID_PARAMETER, 0, 0);
        Fiber_kill();
    }

    FRESULT RET = f_close(&(Files[fd]));
    if (RET == FR_OK) {
        File_BitList[fd] = 0;
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
    
    const void* filename = (void*) args[0];
    // Should I add valid String check here?

    struct sddf_fs_stat_64* file_stat = (void*)args[2];
    FILINFO fileinfo;

    FRESULT RET = f_stat(filename, &fileinfo);
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
    uint64_t fd = args[0];

    const void* oldpath = (void*)args[0];
    const void* newpath = (void*)args[2];
    
    FRESULT RET = f_rename(oldpath, newpath);
   
    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_unlink() {
    uint64_t *args = Fiber_GetArgs();

    const void* path = (void*)(args[0]);

    FRESULT RET = f_unlink(path);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_mkdir() {
    uint64_t *args = Fiber_GetArgs();

    const void* path = (void*)(args[0]);

    FRESULT RET = f_mkdir(path);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_rmdir() {
    uint64_t *args = Fiber_GetArgs();
    const void* path = (void*)(args[0]);

    FRESULT RET = f_rmdir(path);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_opendir() {
    uint64_t *args = Fiber_GetArgs();
    uint32_t fd = Find_FreeDir();
    if (fd == MAX_OPENED_DIRNUM) {
        Function_Fill_Response(args, FR_TOO_MANY_OPEN_FILES, 0, 0);
        Fiber_kill();
    }
    DIR* dir = &(Dirs[fd]);

    FRESULT RET = f_opendir(dir, (void *)args[0]);
    
    // Error handling
    if (RET != FR_OK) {
        Function_Fill_Response(args, RET, 0, 0);
        Fiber_kill();
    }
    
    // Set the position to 1 to indicate this file structure is in use
    Dirs_BitList[fd] = 1;

    Function_Fill_Response(args, RET, fd, 0);
    Fiber_kill();
}

void fat_readdir() {
    uint64_t *args = Fiber_GetArgs();
    
    // Maybe add validation check of file descriptor here
    uint64_t fd = args[0];
    void* name = (void*)args[1];
    
    FILINFO fno;
    FRESULT RET = f_readdir(&Dirs[fd], &fno);
    
    if (RET == FR_OK) {
        strcpy(name, fno.fname);
    }

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_rewinddir() {
    uint64_t *args = Fiber_GetArgs();
    
    // Maybe add validation check of file descriptor here
    uint64_t fd = args[0];

    FRESULT RET = f_readdir(&Dirs[fd], 0);

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_sync() {
    uint64_t *args = Fiber_GetArgs();

    // Maybe add validation check of file descriptor here
    uint64_t fd = args[0];

    FRESULT RET = f_sync(&(Files[fd]));

    Function_Fill_Response(args, RET, 0, 0);
    Fiber_kill();
}

void fat_closedir() {
    uint64_t *args = Fiber_GetArgs();

    // Maybe add validation check of file descriptor here
    uint64_t fd = args[0];

    FRESULT RET = f_closedir(&Dirs[fd]);

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
    
    FRESULT RET = f_readdir(&Dirs[fd], 0);
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