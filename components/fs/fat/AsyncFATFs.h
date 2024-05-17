#include "ff15/source/ff.h"

typedef enum {
	ASYNCFR_OK = 0,				/* (0) Succeeded */
	ASYNCFR_DISK_ERR,			/* (1) A hard error occurred in the low level disk I/O layer */
	ASYNCFR_INT_ERR,				/* (2) Assertion failed */
	ASYNCFR_NOT_READY,			/* (3) The physical drive cannot work */
	ASYNCFR_NO_FILE,				/* (4) Could not find the file */
	ASYNCFR_NO_PATH,				/* (5) Could not find the path */
	ASYNCFR_INVALID_NAME,		/* (6) The path name format is invalid */
	ASYNCFR_DENIED,				/* (7) Access denied due to prohibited access or directory full */
	ASYNCFR_EXIST,				/* (8) Access denied due to prohibited access */
	ASYNCFR_INVALID_OBJECT,		/* (9) The file/directory object is invalid */
	ASYNCFR_WRITE_PROTECTED,		/* (10) The physical drive is write protected */
	ASYNCFR_INVALID_DRIVE,		/* (11) The logical drive number is invalid */
	ASYNCFR_NOT_ENABLED,			/* (12) The volume has no work area */
	ASYNCFR_NO_FILESYSTEM,		/* (13) There is no valid FAT volume */
	ASYNCFR_MKFS_ABORTED,		/* (14) The f_mkfs() aborted due to any problem */
	ASYNCFR_TIMEOUT,				/* (15) Could not get a grant to access the volume within defined period */
	ASYNCFR_LOCKED,				/* (16) The operation is rejected according to the file sharing policy */
	ASYNCFR_NOT_ENOUGH_CORE,		/* (17) LFN working buffer could not be allocated */
	ASYNCFR_TOO_MANY_OPEN_FILES,	/* (18) Number of open files > FF_FS_LOCK */
	ASYNCFR_INVALID_PARAMETER,	    /* (19) Given parameter is invalid */
} ASYNCFRESULT;

#define Status_bit 6
#define First_data_bit 7
#define Second_data_bit 8

# define MAX_FATFS 1
# define MAX_OPENED_FILENUM 128
# define MAX_OPENED_DIRNUM 128

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