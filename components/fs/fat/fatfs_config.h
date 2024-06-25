#define DATA_REGION_SIZE 0x4000000

// The maximum path length, setting this value too large may cause stack overflow
#define MAX_PATH_LEN 512

// Maximum fat partition the file system can have
#define MAX_FATFS 1

// Maximum opened files
#define MAX_OPENED_FILENUM 128

// Maximum opened directories
#define MAX_OPENED_DIRNUM 128