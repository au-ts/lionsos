#include "ff15/source/ff.h"

# define FS_DEBUG_PRINT

#define Status_bit 6
#define First_data_bit 7
#define Second_data_bit 8

void init_metadata(void* fs_metadata);

void fat_mount(void);
void fat_unmount(void);
void fat_open(void);
void fat_close(void);
void fat_stat(void);
void fat_pread(void);
void fat_pwrite(void);
void fat_fsize(void);
void fat_rename(void);
void fat_unlink(void);
void fat_mkdir(void);
void fat_rmdir(void);
void fat_opendir(void);
void fat_closedir(void);
void fat_sync(void);
void fat_seekdir(void);
void fat_readdir(void);
void fat_rewinddir(void);
void fat_telldir(void);

// For debug
#ifdef FS_DEBUG_PRINT
void print_sector_data(uint8_t *buffer, unsigned long size);
#endif