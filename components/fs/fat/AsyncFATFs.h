#include "ff15/source/ff.h"

# define FS_DEBUG_PRINT

#define Status_bit 6
#define First_data_bit 7
#define Second_data_bit 8

void init_metadata(void* fs_metadata);

void fat_mount();
void fat_unmount();
void fat_open();
void fat_close();
void fat_stat();
void fat_pread();
void fat_pwrite();
void fat_rename();
void fat_unlink();
void fat_mkdir();
void fat_rmdir();
void fat_opendir();
void fat_closedir();
void fat_sync();
void fat_seekdir();
void fat_readdir();
void fat_rewinddir();
void fat_telldir();

// For debug
#ifdef FS_DEBUG_PRINT
void print_sector_data(uint8_t *buffer, unsigned long size);
#endif