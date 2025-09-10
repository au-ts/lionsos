#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    printf("WASM: Starting echo server on port %d\n", PORT);
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        printf("WASM|ERROR: socket failed\n");
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (listen(server_fd, 3) < 0) { 
        printf("WASM|ERROR: listen\n");
        return -1;
    }

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        printf("WASM|ERROR: accept\n");
        return -1;
    }

    printf("WASM: Client connected. Echoing messages...\n");
    while (1) {
        int valread = recv(new_socket, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) {
            printf("WASM: Client disconnected or error.\n");
            break;
        }
        printf("WASM: Received: %s\n", buffer);
        send(new_socket, buffer, valread, 0);
        memset(buffer, 0, BUFFER_SIZE);
    }

    close(new_socket);
    close(server_fd);

    return 0;
}
