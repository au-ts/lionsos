#include <stdint.h>

struct sddf_fs_stat_64 {
	uint64_t dev;
	uint64_t ino;
	uint64_t mode;
	uint64_t nlink;
	uint64_t uid;
	uint64_t gid;
	uint64_t rdev;
	uint64_t size;
	uint64_t blksize;
	uint64_t blocks;
	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint64_t atime_nsec;
	uint64_t mtime_nsec;
	uint64_t ctime_nsec;
	uint64_t used;
};

struct stat_response {
    struct sddf_fs_stat_64 stat;
    int status;
};

struct open_response {
    uint64_t fd;
    int status;
};

struct read_response {
    char *data;
    int len;
    int status;
};

extern char *nfs_share;

struct open_response sddf_fs_open(const char *filename);
int sddf_fs_close(uint64_t fd);
struct read_response sddf_fs_pread(uint64_t fd, uint64_t nbyte, uint64_t offset);
int sddf_fs_pwrite(uint64_t fd, const char *buf, uint64_t nbyte, uint64_t offset);
struct stat_response sddf_fs_stat(const char *filename);
int sddf_fs_rename(const char *oldpath, const char *newpath);
int sddf_fs_unlink(const char *path);
int sddf_fs_mkdir(const char *path);
int sddf_fs_rmdir(const char *path);
int sddf_fs_closedir(uint64_t fd);
int sddf_fs_fsync(uint64_t fd);
void sddf_fs_seekdir(uint64_t fd, long loc);
long sddf_fs_telldir(uint64_t fd);
void sddf_fs_rewinddir(uint64_t fd);
int sddf_fs_opendir(const char *in_path, uint64_t *out_fd);
int sddf_fs_readdir(uint64_t fd, char **out_name);
