#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int main(int argc, char **argv) {
    FILE *file = fopen("test.txt", "w");
    if (file == NULL) {
        printf("Error opening file for writing! errno: %d\n", errno);
        exit(1);
    }

    printf("file opened!\n");

    // fprintf(file, "Testing data\n");

    // char buffer[20];
    // while (fgets(buffer, sizeof(buffer), file) != NULL) {
    //     printf("Read: %s", buffer);
    // }


    fclose(file);

    printf("file closed!\n");
    return 0;
}
