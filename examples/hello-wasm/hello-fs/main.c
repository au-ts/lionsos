#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *file = fopen("test.txt", "rw");
    if (file == NULL) {
        printf("Error opening file for writing!\n");
        exit(1);
    }

    fprintf(file, "Testing data\n");

    char buffer[20];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        printf("Read: %s", buffer);
    }

    fclose(file);
    return 0;
}
