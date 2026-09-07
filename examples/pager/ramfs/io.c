#include "decl.h"
#include <microkit.h>
#include "helper.h"
#include <sys/stat.h>
// buffers for filesystem.
// memory regions defined in microkit.
char *input_buffer;  // this will be defined as 0x90000000000 with size = 0x2000
char *output buffer; // this will be defined as 0x90000009000 with size 0x2000

// the parameters will be copied into the microkit message.
// parameters start at 1

long ramfs_fstatat() {
    int dirfd = microkit_mr_get(1);
    const char *path = input_buffer;
    struct stat *statbuf = output_buffer;
}

// TODO: implement
long ramfs_readlinkat() {
    return -EINVAL;
}
long ramfs_openat() {
    int dirfd = microkit_mr_get(1);
    const char *path = input_buffer;
    int flags = microkit_mr_get(2);
}
long ramfs_lseek() {
    long fd = microkit_mr_get(1);
    off_t offset = microkit_mr_get(2);
    int whence = microkit_mr_get(3);
}
long ramfs_mkdirat() {
    int dirfd = microkit_mr_get(1);
    const char *path = input_buffer;
    mode_t mode = microkit_mr_get(2);
    //TODO
    (void)mode;

}
long ramfs_unlinkat() {
    int dirfd = microkit_mr_get(1);
    const char *path = input_buffer;
    int flags = microkit_mr_get(2);
}
long ramfs_write() {
    int fd = microkit_mr_get(1);
    const void *buf = input_buffer;
    size_t count = microkit_mr_get(2);
}
long ramfs_read() {
    int fd = microkit_mr_get(1);
    void *buf = output_buffer;
    size_t count = microkit_mr_get(2);
}
long ramfs_close() {
    long fd = microkit_mr_get(1);
}
long ramfs_dup3() {
    int oldfd = microkit_mr_get(1);
    int newfd = microkit_mr_get(2);
    int flags = microkit_mr_get(3);
}
long ramfs_fstat() {
    int fd = microkit_mr_get(1);
    struct stat *statbuf = output_buffer;
}