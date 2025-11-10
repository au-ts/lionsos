/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main() {
    printf("FILEIO|INFO: fs init\n");

    FILE *file = fopen("hello.txt", "w+");
    assert(file);

    int fd = fileno(file);

    printf("FILEIO|INFO: opened on fd %d\n", fd);

    char *hello = "hello there";
    int size = fwrite(hello, sizeof(char), strlen(hello), file);
    fflush(file);

    printf("FILEIO|INFO: wrote %d bytes\n", size);

    fseek(file, 0, SEEK_END);
    long pos = ftell(file);
    rewind(file);
    printf("FILEIO|INFO: file size is %ld\n", pos);

    char buf[20] = { 0 };
    size_t bytes_read = fread(buf, sizeof(char), pos, file);
    printf("FILEIO|INFO: bytes_read: %d, buf: %s\n", bytes_read, buf);

    printf("FILEIO|INFO: doing fseek\n");
    int err = fseek(file, 100, SEEK_CUR);
    assert(!err);

    pos = ftell(file);

    char *how = "how are you";
    size = fwrite(how, sizeof(char), strlen(how), file);
    fflush(file);

    printf("FILEIO|INFO: wrote %d bytes at pos %ld\n", size, pos);

    int closed = fclose(file);
    assert(closed == 0);

    printf("FILEIO|INFO: closed file\n");

    file = fopen("hello.txt", "r");
    assert(file);

    fd = fileno(file);

    printf("FILEIO|INFO: opened on fd %d\n", fd);
    fseek(file, pos, SEEK_SET);

    memset(buf, 0, sizeof(buf));
    bytes_read = fread(buf, sizeof(char), strlen(how), file);
    printf("FILEIO|INFO: bytes_read: %d, buf: %s\n", bytes_read, buf);

    closed = fclose(file);
    assert(closed == 0);

    const char *dir = "example";
        //rwxr-xr-x
    mode_t mode = 0755;

    if (mkdir(dir, mode) == 0) {
        printf("FILEIO|INFO: Directory '%s' created successfully.\n", dir);
    } else {
        printf("FILEIO|ERROR: Error creating directory\n");
    }

    int dirfd = open(dir, O_DIRECTORY | O_RDONLY);

    printf("FILEIO|INFO: opened directory %s with fd %d\n", dir, dirfd);
    const char *dir2 = "subdir";

    if (mkdirat(dirfd, dir2, mode) == 0) {
        printf("FILEIO|INFO: Subdirectory '%s' created successfully in directory '%s'\n", dir2, dir);
    } else {
        printf("FILEIO|ERROR: Error creating subdirectory\n");
    }

    char *example = "example.txt";
    if ((fd = openat(dirfd, example, O_RDWR | O_CREAT)) > 0) {
        printf("FILEIO|INFO: opened %s at %s with fd %d\n", example, dir, fd);
        int written = write(fd, "hello example", 13);
        printf("FILEIO|INFO: wrote %d\n", written);
        assert(close(fd) == 0);

        char path[40] = "/";
        strcat(path, dir);
        strcat(path, "/");
        strcat(path, example);

        printf("FILEIO|INFO: opening %s\n", path);

        if ((fd = open(path, O_RDONLY)) > 0) {
            printf("FILEIO|INFO: opened %s absolute with fd %d\n", path, fd);
            memset(buf, 0, sizeof(buf));
            bytes_read = read(fd, buf, sizeof(buf));
            printf("FILEIO|INFO: bytes_read: %d, buf: %s\n", bytes_read, buf);
            close(fd);
        }
    }

    close(dirfd);

    return 0;
}
