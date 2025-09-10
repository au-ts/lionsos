#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    printf("opening file for write!\n");

    int fd = open("atest.txt", O_WRONLY | O_CREAT);
    
    printf("file %d opened!\n", fd);

    char *str = "what's up\n";
    int n = write(fd, str, strlen(str));
    printf("file written %d bytes!\n", n);

    close(fd);

    printf("file closed!\n");

    printf("opening file for read!\n");

    fd = open("atest.txt", O_RDONLY);

    printf("file %d opened!\n", fd);
    char buf[20];
    n = read(fd, buf, 20);
    printf("file read %d bytes!\n", n);

    printf("got: %s\n", buf);

    close(fd);

    printf("file closed!\n");

    return 0;
}
